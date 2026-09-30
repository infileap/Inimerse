/* wasm_backend.h - minimal WebAssembly MVP output backend (v0.5 roadmap §2.3)
 *
 * Translates a numeric subset of the AST (int/float/bool, arithmetic,
 * comparisons, if/while/repeat, user functions, globals) into a standalone
 * WebAssembly MVP binary with boxed values in linear memory.  Output IO goes
 * through the fixed import table (docs/WASM.md PAL principle):
 *   env.im_print_int(i64) / env.im_print_float(f64) / env.im_print_bool(i32)
 *   env.im_print_nil() / env.im_error(i32)
 * The host runner is tools/wasm_run.js; equivalence against the interpreter
 * is asserted by tools/wasm_backend.test.py.  SIMD/GC/heaps are future work. */
#ifndef INIMERSE_WASM_BACKEND_H
#define INIMERSE_WASM_BACKEND_H

#include "../parser/ast.h"

/* Compile prog to output_path (.wasm). Returns 0 on success; -1 with a
   message in wasm_backend_last_error() when the program uses constructs
   outside the MVP subset (rejection is explicit, never silent). */
int wasm_compile_program(Program *prog, const char *output_path);
const char *wasm_backend_last_error(void);

#endif /* INIMERSE_WASM_BACKEND_H */
