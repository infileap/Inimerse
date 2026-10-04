/* wasm_backend.h - minimal WebAssembly MVP output backend (v0.5 roadmap §2.3)
 *
 * Translates a numeric subset of the AST (int/float/bool, arithmetic,
 * comparisons, if/while/repeat, user functions, globals) plus arrays into a
 * standalone WebAssembly MVP binary with boxed values in linear memory.
 * Output IO goes through the fixed import table (docs/WASM.md PAL principle):
 *   env.im_print_int(i64) / env.im_print_float(f64) / env.im_print_bool(i32)
 *   env.im_print_nil() / env.im_error(i32)
 * The host runner is tools/wasm_run.js; equivalence against the interpreter
 * is asserted by tools/wasm_backend.test.py (which also asserts the explicit
 * failure modes below).
 *
 * Arrays are heap-allocated in the module's own linear memory: an arena
 * above the globals ("heaps", done), a 16-byte block header carrying size,
 * reference count, free-list link and element count, and a refcounted
 * ownership model that reclaims blocks deterministically at statement
 * granularity.  Arena exhaustion traps through env.im_error(4) and is never
 * silent.  v128 SIMD is implemented (exported bench_sum_scalar /
 * bench_sum_simd pair, ~2x on this host, see docs/WASM.md) but is NOT
 * selected by the code generator - the boxed 16-byte slot layout gives
 * vector stores nothing to save, integer lanes wrap at their own width while
 * the language raises numeric_overflow past int64 (docs/AUDIT.md 1.14), and
 * float reassociation would change results.
 *
 * WebAssembly GC is explicitly NOT implemented: it is a different feature
 * (a toolchain with --enable-gc and struct/array reference types) and it is
 * not what this file's heap does, despite what docs/archive/RELEASE_0.5.0.md
 * implied.  Strings and collections other than arrays remain with the
 * interpreter. */
#ifndef INIMERSE_WASM_BACKEND_H
#define INIMERSE_WASM_BACKEND_H

#include "../parser/ast.h"

/* Compile prog to output_path (.wasm). Returns 0 on success; -1 with a
   message in wasm_backend_last_error() when the program uses constructs
   outside the MVP subset (rejection is explicit, never silent). */
int wasm_compile_program(Program *prog, const char *output_path);
const char *wasm_backend_last_error(void);

#endif /* INIMERSE_WASM_BACKEND_H */
