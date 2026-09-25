# Inimerse 0.4.2 Release Notes

Inimerse 0.4.2 completes the deferred v0.4 Eidos executable subset by moving it into the native C frontend path.

## Changes

- Raw `.im` files can now use the supported `eidos`/`ed` subset directly; `inimerse script.im`, `run`, `buildc`, include/import file loading, debug script loading, and `inimerse --desugar` share the same C `desugar_source` implementation.
- The supported Eidos subset still lowers to factory functions, dictionary instances, and closure methods. This is intentionally not the full class/instance object ABI.
- Supported subset: field defaults, constructor arguments, no-argument and argument methods, single-line methods, `init`/`on_spawn` auto-call, `this`/`self` instance aliases, single inheritance, multiple-parent/mixin composition, method override, limited `super.method(...)`, dot member access, omitted parentheses for no-argument methods, getter/setter properties, constructor/external-assignment `invariant` checks, and basic operator overloading for known Eidos instances.
- Unsupported by design in this release: visibility, sealed/frozen checks, hot modification, native object layout, full dynamic operator dispatch, and full Eidos destructuring.
- Added native regression coverage for `--desugar` output and direct raw Eidos runtime execution.

## Compatibility Notes

`tools/eidos_desugar.py` remains in the tree as a legacy compatibility regression tool. New runtime behavior must be validated through the native engine path.

Existing code that already used the generated factory/dictionary/closure form remains compatible.

## Verification

Local verification on 2026-09-24:

```sh
cmake -S . -B build-0.4.2-final -DINIMERSE_BUILD_ENGINE=ON -DINIMERSE_PACKAGE_VERSION=0.4.2 -DCMAKE_BUILD_TYPE=Release
cmake --build build-0.4.2-final --parallel 2
ctest --test-dir build-0.4.2-final -R 'eidos|lambda|function' --output-on-failure
ctest --test-dir build-0.4.2-final --output-on-failure
node tools/regression.js
cmake --build build-0.4.2-final --target package --parallel 2
find build-0.4.2-final -maxdepth 1 -type f -name 'inimerse-0.4.2-*' -print0 | sort -z | xargs -0 sha256sum > build-0.4.2-final/SHA256SUMS
python3 tools/release_verify.py build-0.4.2-final --version 0.4.2 --require-wasm --require-deb-python3
```

Result: clean Release build reports `inimerse 0.4.2`; 9/9 focused tests passed; 68/68 full CTest passed; 10/10 Node regression tests passed; Linux tar.gz/zip/deb packages and their SHA256 manifest verified. The native desugar regression asserts that member/operator-looking text survives in strings, escaped-quote strings, line comments, and block comments.

The focused Eidos fixture covers multiple-parent/mixin composition, dot fields, getter/setter access, `this`/`self`, `on_spawn`, mixed named/positional construction, basic operator overload dispatch, and invariant checks.

Windows cross-build must be rerun in an environment with the MinGW toolchain before publishing Windows assets; `x86_64-w64-mingw32-gcc` was unavailable during this verification, which covers Linux packages only.
