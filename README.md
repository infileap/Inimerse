# Inimerse / Infiverse

Inimerse is the scripting engine and Infiverse is its Tauri desktop client.

> **状态、路线图与版本裁定**：[docs/STATUS.md](docs/STATUS.md) —— 唯一权威。
> **语言、API 与平台事实**：[docs/API.md](docs/API.md) —— 唯一权威。
> 当前发布基线：**0.5.0**（git tag `v0.5.0`）。v0.6 的「已完成」声明已被逐条废止，清单见 [docs/STATUS.md](docs/STATUS.md) §3.2。

## Layout

- `src/` — C engine: VM, lexer, parser, compiler, optimizer, runtime, platform layer and built-in modules.
- `selfhost/` — self-hosting compiler written in `.im`.
- `mods/` — engine extensions.
- `Infiverse_standard/` — Rust + Tauri desktop application (frozen at v0.3.0).
- `docs/` — current documentation; `docs/archive/` holds historical version notes.
- `future/` — research vision and design drafts (not implementation status).
- `tools/` — Python and Node tooling: benchmarks, bindgen, Wasm host, protocol references and their tests.
- `vtest/`, `scripts/`, `*.im` — examples and regression tests.
- `examples/` — re-homed sample, regression, legacy-UI and benchmark scripts; provenance in [docs/HYGIENE.md](docs/HYGIENE.md) §4/§6.2.

## Build the engine (Linux / POSIX)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -j4     # expected: 100% tests passed, 0 failed out of 94
```

Resulting binary: `build/inimerse`. Always use this path rather than any stale copy in the repository root.

Everything above, plus the §43.5 economy-migration suite, the DSH bridge plugin,
the documentation links and the backtick `docs/…` / `future/…` paths, is one
command: `tools/gate.sh` (see [docs/BOARD.md](docs/BOARD.md) §3).

## Build the engine (Windows)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

## Run and compile scripts

```bash
build/inimerse run script.im
build/inimerse compile script.im out.inim
build/inimerse buildc --incremental script.im out.inim
```

The P1 minimal-Layer loop is two separate processes that talk only over stdin/stdout:

```bash
build/inim-server root main             # authoritative side: canonical-JSON request per line
build/inim-client root main scenario    # forks its own inim-server and drives it
```

`inim-server` assigns `seq`/`rev` itself and refuses any request that tries to supply them; it refuses to open a session at all when the durable commit pointer does not match the log. See [docs/API.md](docs/API.md) §10.6.

JS-side protocol tests are not part of CTest and must be run separately:

```bash
node tools/upp_reference.test.js
node tools/crp_reference.test.js
node tools/vverse_validate.test.js
```

## Build the desktop client

Frozen at v0.3.0; no new features. See [docs/STATUS.md](docs/STATUS.md) §6.

```powershell
cd Infiverse_standard\src-tauri
cargo build --release --offline
```

## Create the Windows installer (legacy)

Install Inno Setup 6, then run:

```powershell
& 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe' installer.iss
```

This path is Windows-only and still contains local absolute paths; it is not part of the current delivery line.

## Data and secrets

Runtime data is stored in `userdata/` and is intentionally not tracked. OAuth client secrets must be supplied through environment variables; never commit them to the repository.

## Documentation

| Document | Purpose |
| --- | --- |
| [docs/STATUS.md](docs/STATUS.md) | Status, roadmap and version rulings (single source of truth) |
| [docs/API.md](docs/API.md) | Language, built-ins, syntax sugar, platform support |
| [docs/REQUIREMENTS_ANALYSIS.md](docs/REQUIREMENTS_ANALYSIS.md) | Requirements vs. reality gap matrix and document conflicts |
| [docs/archive/README.md](docs/archive/README.md) | Historical version notes and the list of retracted claims |
| [future/README.md](future/README.md) | Research vision and design drafts |

## License

See [LICENSE](LICENSE).
