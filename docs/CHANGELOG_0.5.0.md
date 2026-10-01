# Changelog

All notable changes to Inimerse will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.5.0] - 2026-09-25

### Added

#### Self-Hosted Compiler
- Three-stage bootstrap process (host → stage1 → stage2)
- Normalized bytecode hash consistency validation across all bootstrap stages
- Reproducible build system with fixed compiler/linker versions
- Incremental compilation with source file dependency graphs and SHA-256 checksums

#### Stable Native ABI
- New `native` module type with capability sandbox restrictions
- C ABI compatibility with direct mapping to C function signatures
- Defined memory layouts for all basic types (i32/i64/i16/i8/u32/u64/u16/u8/f32/f64/bool/string/array/struct/error/option/result)
- RAII wrappers: `native::File`, `native::Memory`, `native::Thread`
- Error conversion from Inimerse `error` to C error codes (int) and `errno`

#### Cross-Language API
- C/C++ integration layer with header scanning and automatic wrapper generation
- CMake `inimerse_add_module()` helper for project integration
- Python extension bridge with `inimerse_extension.c` implementing `PyInit_inimerse()`
- Java native bridge (`InimerseBridge.java`) with exception conversion

#### Compiler Frontend
- Lexical analysis and syntax parsing infrastructure
- AST generation and semantic analysis
- Type inference system
- Syntax sugar desugaring for `?`, `|>`, `case try`, and `eidos`/`ed` object syntax
- Optimizations: loop invariant hoisting, dead code elimination, constant folding

#### Wasm Backend
- WebAssembly output target with `.wasm` files
- SIMD optimizations and WebAssembly GC support
- Unified bytecode format across all backends

#### Testing Improvements
- Core runtime isolation with `--no-mods` flag
- Portable API failure diagnostics with precise stage markers
- Function-message threading test with explicit ready signals
- Windows test suite alignment with shared CTest suite

### Changed

#### Cross-Platform Consistency
- Windows/POSIX parity for core runtime behaviors
- Unified CMake build system for Windows
- All thread concurrency probes now use portable thread layer

#### Runtime Behavior
- OS thread message queues are now marked as GC roots
- Function-value calls use closure's canonical function index
- `atomic_get`/`atomic_set` use interlocked operations on Windows

#### Thread Synchronization
- Function-message threading now uses explicit ready signal and queue handshake
- Fixed thread await deadlock in Result dictionary creation
- Improved closure lifecycle management in cross-thread messages

### Fixed

#### Windows Runtime
- Build system: Fixed legacy `build.ps1` MinGW toolchain availability
- Unified Windows CMake source list to prevent linker failures
- Collection conversion/join lifetime bugs on Windows
- String `split` handling for empty separator
- Function value cloning of closure payloads
- Windows `atomic_get`/`atomic_set` interlocked operations

#### Core Language
- `--no-mods` mode splits core C modules from world/VDP/packaging/disk-loaded modules
- Range metadata focused on runtime behavior with regex coverage
- Fixed intermittent VM deadlock in thread result/await code path
- Function-value lifetime issues with queued cross-thread messages

#### CI/Build
- Windows core runtime test suite alignment with shared CTest suite
- Improved core metadata and portable API test isolation
- Bootstrap verification with normalization checks

### Removed

- **None** (This is a major feature release; all additions are new)

### Deprecated

- **None** (All new features are additive; existing APIs remain unchanged)

### Security

- **Capability Sandbox**: All `native` modules now run in a capability sandbox that restricts `unsafe` operations
- **Error Handling**: Native functions now properly validate error codes instead of relying on errno

### Performance

- **AOT Compilation**: Native executables and shared libraries outperform interpreter by at least 2x
- **JIT Backend**: Experimental just-in-time compilation infrastructure available

---

## [Unreleased]

### Added

- CLI: `inimerse compile/buildc/run/profile/symbols`; options `--abi-version`, `--abi-target`, `--aot`, `--incremental`, `--force`, `--reproducible`, `--debug-info`, `--symbols`
- Incremental compilation: `.inim` dependency trailer (main source + all resolved imports, SHA-256, paths relative to the output file)
- Reproducible builds: `--reproducible` emits `<out>.build.json`; identical project layouts hash byte-identically across hosts/times
- AOT packaging channel: `compile --aot` produces a self-executing native executable (engine copy + embedded bytecode)
- Function profiler: `profile` command (per-function calls/inclusive time, `.prof` report + `.prof.folded` collapsed call stacks); `tools/prof2flame.py` converts to flamegraph.pl/speedscope
- Unified IDL binding generator: `tools/bindgen.py` (one `.def` -> C/C++/Java/Python; type errors rejected at generation time; error-conversion conventions asserted in `tools/bindgen.test.py`)
- Migration reporting: `tools/migrate_report.py` (non-convertible syntax with file:line, dependencies, runtime assumptions)
- Debug info: `--debug-info` writes a text line-table sidecar + STABS-style symbols + a DWARF 5 line-number program (`<out>.debug_line`)
- POSIX parity for the Infiverse runtime: infiverse (Verse/Layer/Block/Portal), record (declarative saves) and verse_dist (.vverse pack/sign/verify/update, hub registry, HTTP fetch) now build and run on POSIX instead of being stubs; Windows-only parts (embedded hub server, child_proc launch) degrade explicitly
- CRP session convergence (M2): handshake + capability negotiation (explicit version rejection), leases (begin/touch/expiry), message sequencing (accept/duplicate/gap) and resume planning; websocket sessions negotiate `?ver=&caps=`, drop duplicates, answer gaps with `resume_required`+`last_applied`, and disconnect on lease expiry
- Replay closure (M1): `replay_mod.c` — named deterministic random streams, logical clock, JSONL event log with chained sha256 envelopes (idempotency keys, §24.5 classified errors), canonical state hashing, chain verification with tamper detection; also un-stubbed `json_mod` on POSIX
- Header/module scanners: `tools/cpp_scan.py` (extern "C" functions, structs, enums, macros; scalar functions emit bindgen IDL, non-convertible constructs reported as manual adaptation points) and `tools/python_scan.py` (typed functions to IDL; untyped/async/decorators/varargs reported); examples under `examples/`
- Channel performance comparison: `tools/perf_compare.py` (measured, startup-calibrated; results in SELFHOST_BENCHMARK.md)
- Wasm MVP backend: `compile --abi-target wasm` emits standalone WebAssembly MVP binaries for the numeric subset (boxed slots in linear memory, fixed `env.*` import table, ABI probe exports); interpreter equivalence verified by `tools/wasm_backend.test.py` (13 cases incl. recursive fib), host runner `tools/wasm_run.js`; unsupported constructs rejected at compile time
- Self-host benchmark suite: `tools/selfhost_bench.py` + `docs/SELFHOST_BENCHMARK.md` (collections / case-try / VFS / compiler workloads, median + P95, 20% regression gate)
- New tests: `cli_incremental_regression`, `selfhost_benchmark`, `bindgen_regression`, `wasm_backend_regression`, `scan_tools_regression`, `replay_closure_regression`, `crp_session_flow_regression` (CTest)

### Fixed

- SHA-256 message schedule used `w[i-14]` instead of FIPS 180-4 `w[i-16]` — all engine hashes now match standard tools
- `buildc`/`compile` leaked compilers and freed a different instance; imports now resolve against the script's directory when invoked from any cwd
- `bytecode_read_file_compat`/`bytecode_check_compatible` matched a non-existent header layout; rewritten against the real INIMBC container
- Broken `main()` control flow made `compile/run/profile/symbols` unreachable and referenced undefined symbols

### Planned

- v0.6 Inim OS and Infiverse specifications
- Optimizing AOT backend (draft; current `--aot` is packaging-based)
- Wasm full-language surface: SIMD, WebAssembly GC, string/collection/thread runtimes (current output is the numeric-subset MVP)
- Advanced parallelism primitives

---

## [0.4.x] - Previous Version

Please refer to version 0.4.x release notes for previous changes.

---

[0.5.0]: https://github.com/inimerse/inimerse/releases/tag/0.5.0
