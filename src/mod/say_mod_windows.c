#include "vm.h"
#include <stdio.h>
#include <string.h>

static void drop(VM *vm) { int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1); }
static const char *arg(VM *vm) { if (vm->cur_argc <= 0 || vm_cur_sp(vm) < 0) return ""; Value v = vm_cur_stack(vm)[vm_cur_sp(vm)]; return v.type == VAL_STRING && v.sval ? v.sval : ""; }
static int say_console(VM *vm) { const char *s = arg(vm); drop(vm); puts(s); fflush(stdout); return 0; }
static int say_target(VM *vm) { const char *s = arg(vm); drop(vm); puts(s); fflush(stdout); return 0; }
void say_mod_register(VM *vm) {
    vm_register_builtin(vm, "say.console", say_console); vm_register_builtin(vm, "say_console", say_console);
    vm_register_builtin(vm, "say_target", say_target);
}
