# Inimerse / Infiverse

Inimerse is the scripting engine and Infiverse is its Tauri desktop client.

## Layout

- `src/` — C engine, VM, parser, runtime and built-in modules.
- `selfhost/` — self-hosting compiler and language tools.
- `mods/` — engine extensions.
- `Infiverse_standard/` — Rust + Tauri desktop application.
- `docs/` — API, protocol and roadmap documentation.
- `scripts/` and `*.im` — examples and regression tests.

## Build the engine

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

The build writes `inimerse.exe` locally. Build outputs and user data are ignored by Git.

## Build the desktop client

```powershell
cd Infiverse_standard\src-tauri
cargo build --release --offline
```

## Create the Windows installer

Install Inno Setup 6, then run:

```powershell
& 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe' D:\inimerse_stable\installer.iss
```

The installer is written outside the repository to `D:\Infiverse_release\InfiverseSetup.exe`.

## Data and secrets

Runtime data is stored in `userdata/` and is intentionally not tracked. OAuth client secrets must be supplied through environment variables; never commit them to the repository.

## License

See [LICENSE](LICENSE).
当前稳定版本：**0.2.0**。发布说明见 [docs/RELEASE_0.2.0.md](docs/RELEASE_0.2.0.md)。
