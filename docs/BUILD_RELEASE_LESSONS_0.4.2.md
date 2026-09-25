# Inimerse 0.4.2 Build Lessons

## What Changed

The Eidos executable subset should be treated as an engine frontend feature, not as a Python preprocessing workflow. The durable fix is to keep one C implementation behind both file loading and `--desugar`, then test that raw `.im` files run directly.

## Rules For Next Time

1. Do not validate a language feature only through an external translator. Add at least one CTest that feeds raw source into `inimerse`.
2. Keep legacy translator tests only as compatibility guards. They must not be the release blocker that proves native behavior.
3. For syntax sugar that scans strings, comments, braces, and inheritance, add a `--desugar` output test and a runtime test. Include escaped quotes and text that resembles member/operator syntax inside strings and comments.
4. When bumping release version, reconfigure the build directory with `-DINIMERSE_PACKAGE_VERSION=<version>`; changing the CMake default does not update an existing cache.
5. Document the representation boundary explicitly. For v0.4.2, Eidos is native frontend sugar over factory/dictionary/closure runtime values, not the final object ABI.
6. Keep the language fixture limited to VM-supported primitives. A property test that calls an
   unavailable helper such as `max` can make a correct setter look broken; isolate frontend
   behavior from unrelated builtin coverage.
7. When adding inheritance composition, test both the generated helper body and the final factory
   closure. Mixin methods must be rewritten against the composed class's effective fields, while
   an undeclared parent must fail with a source diagnostic.
8. Operator overload syntax needs two checks: safe internal method names in generated C-compatible
   helpers, and source-level dispatch only when the left operand is a statically known Eidos
   instance. Do not rewrite ordinary numeric or string operators.
9. Lifecycle hooks should be lowered at construction time only when their signature is known.
   The current stable hook is zero-argument `on_spawn`; keeping this explicit avoids silently
   inventing scheduling semantics for `on_update` or thread-bound hooks.
10. Keep source-level object aliases (`this`/`self`) in the same reference rewrite as fields and
    methods. That preserves one instance representation and avoids introducing a second hidden
    receiver convention.
11. Land invariants only where the frontend can prove the mutation boundary. Constructor completion
    and rewritten external dot/setter assignments are stable; full method-internal automatic checks
    belong with the future object ABI.
12. Do not trust a pre-existing build directory just because its source path looks right. Check its
    CMake cache, then configure a clean version-specific directory and build, test, package, and
    verify artifacts from that same tree. Generate the checksum manifest from those exact artifacts.
13. When promoting a frontend value to a VM-native object, update every value boundary in one
    change: equality, register/argument stack transfer, indexing, GC marking, stringification,
    debug type reporting, and both runtime backends. Missing `L_PUSH_REG` support is especially
    deceptive because construction appears to work until an object crosses a function or closure
    boundary.
14. Keep the first native object ABI deliberately narrow: `VAL_OBJECT` carries a stable class name
    and pool handle, while the pool may temporarily store dictionary-shaped fields. Expose
    `type/str/len/has` semantics and identity tests now, then replace the field pool with class
    descriptors and offsets later. Do not claim that this transitional representation is already
    the final vtable or visibility ABI.

## Commands

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

## Follow-Up Boundary

The next real Eidos milestone is not more desugar expansion. It is a designed class/instance/method ABI with source diagnostics, ownership of method environments, object layout, and compatibility rules for mixins and hot modification.
