# Inimerse 0.5.0 Release Notes

Inimerse 0.5.0 is a major feature release that introduces the self-hosted compiler infrastructure and stable Native ABI, establishing Inimerse as a foundation for cross-language integration and compilation pipelines.

## Features

### Self-Hosted Compiler

- **Three-stage bootstrap process**: Host C++ compiler generates initial Inimerse bytecode, stage1 compiler compiles host bytecode to first bootstrap output, stage2 compiler compiles itself to final bootstrap output
- **Verifiable bootstrap**: Normalized bytecode hash consistency validation across all three stages
- **Reproducible build system**: Fixed compiler versions, linker versions, randomized seed disabling, and path locking for fully reproducible builds
- **Incremental compilation**: Source file dependency graphs with SHA-256 checksums, incremental build flags, and rebuild-on-change semantics

### Stable Native ABI

- **Native module declaration**: New `native` module type with capability sandbox that restricts `unsafe` operations
- **C ABI compatibility**: Function signatures map directly to C ABI conventions (i32/int, i64/long, f64/double, string/char*+length)
- **Memory layout specification**: Defined layouts for integers, floating point, strings, arrays, structs, errors, options, and results
- **RAII bindings**: `native::File`, `native::Memory`, `native::Thread` wrappers with automatic resource management
- **Error conversion**: Inimerse `error` values convert to C error codes (0=success, non-zero=error) or C `errno` set to error numbers

### Cross-Language API

- **C/C++ integration layer**: Header file scanning tool for identifying `extern "C"` functions, structures, enums, and macros with automatic wrapper generation
- **CMake project integration**: `inimerse_add_module()` helper that handles linking, include paths, and symbol export
- **Python extension bridge**: `inimerse_extension.c` implementing `PyInit_inimerse()` for CPython/ABI3 extension module support
- **Java native bridge**: `InimerseBridge.java` for JVM-native calls with exception conversion between Java exceptions and Inimerse errors

### Compiler Frontend

- **Lexical and syntactic analysis**: Tokenization, parsing, AST generation, and semantic analysis infrastructure
- **Type inference**: Automatic type deduction across expression patterns
- **Syntax sugar desugaring**: Support for `?` null propagation, `|>` pipeline operators, `case try` pattern matching, and `eidos`/`ed` object syntax
- **Optimizations**: Loop invariant hoisting, dead code elimination, constant folding

### Wasm Backend

- **WebAssembly output**: New `.wasm` target supporting SIMD optimizations and WebAssembly GC
- **Unified bytecode format**: All backends (interpreter, AOT, Wasm) share the same bytecode format and ABI for consistent execution

## Performance

- **AOT compilation**: Native executable and shared library outputs (`inimerse.aot`, `libinimerse.so`) that outperform the interpreter by at least 2x
- **JIT template backend**: Experimental just-in-time compilation infrastructure for dynamic code generation

## Improvements

### Cross-Platform Consistency

- **Windows/POSIX parity**: All core runtime behaviors now match across platforms including collection metadata, process probing, and thread message timing
- **Unified CMake build system**: Single Windows CMake source list in `build.ps1` prevents linker failures when adding new modules
- **Portability improvements**: All thread concurrency probes use the portable thread layer, building successfully with both MinGW and POSIX toolchains

### Runtime Stability

- **Fixed thread await deadlock**: Resolved issue where thread result/await code created Result dictionaries while worker threads were still active
- **Closure lifecycle fixes**: Queued cross-thread function messages now correctly clone closure payloads and OS thread message queues are marked as GC roots
- **Function call normalization**: Function-value calls use the closure object's function index as the canonical target
- **Windows atomic operations**: `atomic_get`/`atomic_set` now use interlocked operations for proper cross-thread visibility

### Testing and CI

- **Core runtime isolation**: `--no-mods` flag properly isolates core C modules while keeping world, VDP, packaging, and disk-loaded modules opt-out
- **Improved test diagnostics**: Portable API failures report precise stage markers without tripping CTest on diagnostic string-table output
- **Function-message threading test**: Replaced fixed startup sleep with explicit ready signal and queue handshake, verifies results through `thread_result` instead of `gui_say`
- **Windows test suite stabilization**: Collection conversion/join lifetime bugs, process probing, and thread message timing now match the shared CTest suite

## Fixes

### Windows Runtime

- **Build system**: Fixed legacy `build.ps1` MinGW toolchain availability and unified Windows CMake source list
- **Collection handling**: Fixed Windows collection conversion/join lifetime bugs
- **String operations**: Fixed `split` handling for empty separator to match portable runtime whitespace split behavior
- **Function messaging**: Made copied function values clone their closure payload and OS thread message queues mark queued function values as GC roots
- **Thread synchronization**: Fixed Windows `atomic_get`/`atomic_set` to use interlocked operations, made closure calls use canonical function index

### Core Language

- **No-mods mode**: Split `--no-mods` so core C modules still register while world/VDP/packaging/disk-loaded modules stay out of core tests
- **Range metadata**: Focused range metadata checks on runtime behavior with regex coverage in portable API tests
- **VM deadlock**: Fixed intermittent VM deadlock in thread result/await code path
- **Function-value lifetimes**: Fixed issues with function-value calls and queued cross-thread messages

### CI/Build

- **Test failures**: Fixed Windows core runtime test suite alignment with shared CTest suite
- **Test isolation**: Improved isolation of core metadata and portable API tests
- **Bootstrap verification**: Added normalization checks and validation to bootstrap process

## Breaking Changes

### ABI Versioning

- **Bytecode version**: New bytecode format `infiverse.mv1` with version number in header
- **ABI version**: New ABI version `infiverse.mv1/abi/1.0` with strict version compatibility checks
- **Build flag**: Added `--abi-version` flag to control target ABI version; non-matching versions fail at build time

### Native Module Declaration

- **Old behavior**: Direct C function calls without declared module interface
- **New behavior**: All C-interfaced functions must be declared in a `native` module with capability sandbox restrictions

### Thread Synchronization

- **Thread message queues**: OS thread message queues are now marked as GC roots; queued function values remain callable until consumed by receiver
- **Ready signal**: Function-message threading now uses explicit ready signal and queue handshake instead of fixed sleep delays

## Assets

This release publishes the following assets:

### Runtime Packages (Linux)

- `inimerse-0.5.0-Linux-x86_64.tar.gz`
- `inimerse-0.5.0-Linux-x86_64.zip`
- `inimerse-0.5.0-Linux-x86_64.deb`
- `SHA256SUMS`

### Cross-Language Bindings

- **C/C++ headers**: `inimerse-native.h` with RAII wrappers and CMake helpers
- **Python extension**: `inimerse-0.5.0-py3-none-any.whl` with CPython/ABI3 support
- **Java bridge**: `inimerse-0.5.0-jar-with-dependencies.jar` with Gradle/Maven plugin

### Compiler Toolchain

- **Bootstrap compiler**: `inimerse-bootstrap` stage1 and stage2 binaries
- **Compiler frontend**: `inimersec` command-line compiler for building `.im` to bytecode
- **AOT compiler**: `inimerse-aot` for generating native executables and shared libraries

## Migration Guide

### From 0.4.x to 0.5.0

1. **Update compiler**: Install the new `inimersec` compiler or upgrade your bootstrap toolchain
2. **Specify ABI version**: Add `--abi-version infiverse.mv1/abi/1.0` to your build commands if targeting the new ABI
3. **Declare native modules**: Wrap all C-interfaced functions in `native` module declarations with capability sandboxes
4. **Update bindings**: Replace old direct C calls with the new RAII wrappers (`native::File`, `native::Memory`, etc.)
5. **Test cross-language**: Verify your C/C++/Python/Java bindings work with the new error conversion conventions

### Breaking API Changes

- **Error return types**: Native functions now return error codes (int) instead of `void` with error set to `errno`
- **Function closures**: Function values must be called using the closure's canonical function index
- **Thread message queues**: OS thread message queues are now managed by the runtime; do not manually manage function message lifetimes

## Documentation

- **[API Reference](API_REFERENCE.md)**: Complete API documentation with Native ABI details
- **[API Catalog](API_CATALOG.md)**: State index of all public APIs
- **[API Builtin Table](API_BUILTIN_TABLE.md)**: Built-in functions organized by category
- **[Self-Hosted Benchmark](SELFHOST_BENCHMARK.md)**: Bootstrap process results and performance metrics
- **[Portability](PORTABILITY.md)**: Cross-platform compatibility notes
- **[WASM/WASI](WASM.md)**: WebAssembly compilation and runtime details

## Roadmap

v0.5.0 lays the groundwork for:
- **v0.6**: Inim OS and Infiverse specifications as operating-level services
- **Frontier capabilities**: Advanced parallelism, concurrency primitives, and language features

See [ROADMAP_0.5-0.6.md](ROADMAP_0.5-0.6.md) for detailed version planning.
