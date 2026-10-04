# dsh-inimerse — Inimerse / Infiverse harness bridge

A DSH (Cordis) **Host-only bundle** that turns the Inimerse engine from "something
a session shells out to with ad-hoc `bash`" into a first-class harness capability.

Instead of every session re-deriving the same pipelines — locate the checkout,
configure CMake, parse `ctest` output, remember the `inim-server` line protocol —
the five tools below are declared once and return **structured JSON**. A session
learns the engine's real state (warnings, failing test names, server-assigned
sequence numbers, protocol refusals) as data, not as scraped text.

## The harness model

| Layer | What it is | Where it lives |
| --- | --- | --- |
| Engine | The Inimerse interpreter, compiler and P1 Verse runtime | `src/`, built into `build/` |
| Boundary | `inim-server` / `inim-client`: newline-delimited canonical JSON over stdin/stdout | `src/verse/{server,client,protocol}.c` |
| Harness | DSH driving the engine as a declared capability | this bundle |

The bundle is the top layer only. It owns **no** engine logic: every mutation it
performs goes through the real binaries, so the boundary rules the engine
enforces — the server assigns `seq`/`rev`, a client may not assert final state,
an unanchored log refuses a session — apply unchanged. Several tools exist
precisely to make those refusals *visible* rather than to paper over them.

## Tools

### `inim_status` — read-only project snapshot

Engine version, whether the engine / `inim-server` / `inim-client` binaries
exist, git branch + last commit + dirty files + ahead/behind, the CTest count
declared in `CMakeLists.txt`, and the current files under `docs/`.

Concurrency-safe. Call it before building or testing to avoid acting on a stale
tree.

### `inim_build` — configure and build

`cmake -S <repo> -B <build> -DCMAKE_BUILD_TYPE=Release` followed by
`cmake --build <build> -j`. Returns both steps' exit codes and durations,
**warning and error counts**, and the tail of the log.

Options: `clean` (wipe the build directory first — the release-gate path),
`skip_configure`, `target`, `jobs`, `build_type`, `build_dir`.

Not concurrency-safe: two builds in one directory fight over the same files.

### `inim_test` — run the CTest suite

Returns `total` / `passed` / `failed` and the **names of the failing tests**,
parsed from CTest's own output. Options: `filter` (`--tests-regex`), `label`
(`--label-regex`), `jobs`, `output_on_failure` (default on).

The plugin reports CTest's verdict and does not re-judge it. In particular a test
registered `WILL_FAIL TRUE` is *expected* to fail and CTest counts it as passing;
silently inverting that here would hide the distinction the registration encodes.
(This repository currently has no such test — `economy_migration_regression` was
the one, until its defect was fixed and the marker removed.)

### `inim_run` — execute a script

Either `script` (a `.im` file in the checkout, absolute or relative) or `source`
(an inline snippet written to a scratch file under the build directory).
Returns stdout, stderr, exit code, duration and timeout status.

Note that the engine prints module-load notices on stdout before the program's
own output; the tool passes the stream through unmodified rather than filtering
it, because a filter that guesses which lines are noise would eventually eat a
program's real output.

### `inim_verse` — drive a Verse layer across the real process boundary

Starts `inim-server <root> <verse_id>`, performs the `hello` handshake, applies
the requested `ops` in order, and closes with `bye`. Returns the **verbatim
protocol transcript**.

```jsonc
// op = put
{ "op": "put", "key": "idem-key", "cell": "alpha", "value": 7 }
// op = undo — target is the sequence number to revert
{ "op": "undo", "key": "idem-key", "target": 1 }
// also: { "op": "status" }, { "op": "drain" }, { "op": "bye" }
```

The tool never sends `seq`, `rev`, `head`, `balance`, `state_hash` or
`committed`: those are the server's to assign, and a request carrying one is
refused with `client_authority` by design. Round-tripping that refusal is part of
the verification.

Two session rules are load-bearing and reflected in the output:

- `put` / `undo` require a successful `hello`, otherwise `no_session`.
- `drain` / `status` are deliberately reachable **without** a session. When an
  anchor mismatch blocks `hello`, an operator still needs to inspect and advance
  recovery — and `status` carries its own anchor gate, so it refuses rather than
  presenting tampered values as committed.

`anchorMismatch: true` means the server exited **2**, its own signal that the
durable commit pointer no longer matches the log.

## Configuration

The bundle declares no `Config` schema on purpose: a Host-only bundle that
imports nothing cannot validate one, and a half-checked schema would be worse
than reading defensively. Every key is optional and falls back to a default.

| Key | Default | Meaning |
| --- | --- | --- |
| `repoRoot` | `/home/sakiko/inimerse` | checkout the tools operate on |
| `buildDir` | `<repoRoot>/build` | CMake build directory |
| `serverBin` | `<buildDir>/inim-server` | `inim-server` binary |
| `jobs` | `0` (all cores) | parallel job count |
| `timeoutMs` | `120000` | default wall-clock limit per call |
| `env` | `{}` | extra environment for child processes |

## Install

Installed with the Plugin Manager as a **local bundle**, pointed at this
directory — not by editing the profile's `package.json` or running a package
manager inside the profile:

```
plugin_manager { action: "install_bundle", target: "/home/sakiko/inimerse/tools/dsh-inimerse" }
```

A newly installed bundle can activate through HMR; confirm the rows with a
read-only inspect query before reporting success.

## Verify

```
node tools/dsh-inimerse/verify.mjs          # offline: schemas + status + error paths
node tools/dsh-inimerse/verify.mjs --live   # also runs a script and a Verse round trip
node tools/dsh-inimerse/verify.mjs --live --build   # also rebuilds (minutes)
```

`verify.mjs` loads the plugin outside DSH, captures the registrations, and
asserts each output schema against the registry's own `assertSupportedJsonSchema`
— the exact check `ctx.tools.register` runs, which aborts activation when it
fails. It then executes the tools and validates each returned value against its
own schema, because a value the schema rejects turns a *successful* call into
`INVALID_TOOL_OUTPUT`. Live checks are idempotent: they wipe their Verse root
first, so sequence numbers start at 1 on every run.

The verifier always drives **the checkout it lives in**, not the `repoRoot` in
`cordis.patch.yml`: that key is an absolute path for the *installed* plugin, so
honouring it here would make a worktree or a fresh clone verify some other tree —
and a stale engine there passes while the tree under test is never touched. The
config is still read, and a mismatch is printed as a `note:` line. `repoRoot`
below therefore describes the installed plugin's behaviour, not this script's.

## Design notes

- **No dependencies, no build step.** The module imports only Node builtins, so
  the loaded file is the authored file. This is why the config is read
  defensively and the tool schemas are plain JSON Schema rather than
  `defineTool` spec objects.
- **Expected failures are values, not throws.** A missing binary, a failing
  build, a timed-out suite and a refused protocol op all return `ok: false` with
  a machine-readable `code` and the engine's own diagnostics. Throwing would
  replace the engine's error text with a stack trace.
- **Nothing is inferred.** `inim_verse` reports the JSON lines the server
  produced, in order. It does not synthesise a summary of a mutation it did not
  see acknowledged.
- **A cancelled call kills its child.** A build that outlived its tool call
  would keep writing to the build directory while a later call reads it.
