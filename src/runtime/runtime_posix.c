#include "runtime.h"
#include <stdlib.h>
#include <math.h>

static int posix_random(VM *vm) { if (vm_cur_sp(vm) < 0) return 0; int n = vm_cur_stack(vm)[vm_cur_sp(vm)].ival; pop(vm); push_int(vm, n > 0 ? rand() % n : 0); return 1; }
static int posix_sqrt(VM *vm) { if (vm_cur_sp(vm) < 0) return 0; Value v = vm_cur_stack(vm)[vm_cur_sp(vm)]; pop(vm); push_float(vm, sqrt(v.type == VAL_INT ? (double)v.ival : v.fval)); return 1; }

/* POSIX baseline: platform-specific runtime builtins are intentionally
 * unavailable until their PAL backends are implemented. Core VM and
 * language builtins remain usable; callers receive a clear unsupported
 * capability instead of a link-time/platform failure. */
void runtime_register_builtins(VM *vm) {
    vm_register_builtin(vm, "random", posix_random);
    vm_register_builtin(vm, "sqrt", posix_sqrt);
}

void record_load_from_file(VM *vm, const char *path) { (void)vm; (void)path; }
void record_save_to_file(VM *vm, const char *path) { (void)vm; (void)path; }
