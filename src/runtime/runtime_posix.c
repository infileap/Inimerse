#include "runtime.h"

/* POSIX baseline: platform-specific runtime builtins are intentionally
 * unavailable until their PAL backends are implemented. Core VM and
 * language builtins remain usable; callers receive a clear unsupported
 * capability instead of a link-time/platform failure. */
void runtime_register_builtins(VM *vm) {
    (void)vm;
}

void record_load_from_file(VM *vm, const char *path) { (void)vm; (void)path; }
void record_save_to_file(VM *vm, const char *path) { (void)vm; (void)path; }
