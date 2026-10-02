#ifndef AOT_NATIVE_H
#define AOT_NATIVE_H

#include "ast.h"

/* ---------------------------------------------------------------------------
 * aot_native — a real (if deliberately small) native code generator.
 *
 * The `--aot` channel in src/main.c is *packaging*, not code generation: it
 * copies the engine executable and appends bytecode, so the product runs
 * through the very same C interpreter (see src/compiler/bytecode.c
 * bytecode_append_to_exe).  This module is the other thing: it lowers the
 * numeric subset of the AST to a standalone C translation unit that the host
 * `cc` compiles to a native executable.
 *
 * The accepted subset is deliberately the one the Wasm backend already
 * defines (src/compilation/wasm_backend.h): int/float/bool values, arithmetic
 * and comparison operators, `if` / `while` / `repeat`, user functions and
 * globals.  Anything outside it is REFUSED with a reason rather than
 * mis-compiled -- a wrong answer is worse than a refusal.
 *
 * This header declares the whole interface; nothing here allocates a global,
 * touches the filesystem beyond `out_path`, or depends on the engine runtime.
 * ------------------------------------------------------------------------- */

/* Translate `prog` to C source at `out_path`.
 * Returns 1 and writes the file on success.
 * Returns 0 and sets aot_native_last_error() when the program leaves the
 * subset or the file cannot be written.  On failure `out_path` is left absent
 * rather than half-written. */
int aot_native_translate(Program *prog, const char *out_path);

/* Same, with a harness hook — and the hook is not optional decoration, it is
 * what makes the backend *measurable*.
 *
 * The host compiler sees the whole generated translation unit.  If every input
 * is a literal it proves the result at compile time: a `sum(2000000)` workload
 * compiles to a stored constant, the binary does no work at all, and any
 * speedup measured against it is meaningless.  Passing `extern_name` declares
 * that source-level global as `extern` (the linking harness defines it) and
 * `entry_name` renames the generated entry point, so the workload's bound can
 * be supplied at run time and the loop survives optimisation.
 *
 * `extern_name` of NULL and `entry_name` of "main" reproduce
 * aot_native_translate() exactly. */
int aot_native_translate_ex(Program *prog, const char *out_path,
                            const char *extern_name, const char *entry_name);

/* Reason the last aot_native_translate() returned 0.  Never NULL. */
const char *aot_native_last_error(void);

/* One-line description of the accepted subset, for docs and diagnostics. */
const char *aot_native_subset_description(void);

#endif /* AOT_NATIVE_H */
