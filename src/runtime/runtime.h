#ifndef RUNTIME_H
#define RUNTIME_H

#include "vm.h"
void runtime_register_builtins(VM *vm);

/* The one implementation of the `vm_exec` builtin (vm_exec_builtin.c).
 * Shared by the WIN32 runtime (runtime.c) and the POSIX runtime
 * (runtime_posix.c) so the self-hosted compiler's output path works on both. */
int im_builtin_vm_exec(VM *vm);

#endif