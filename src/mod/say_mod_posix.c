#include "vm.h"
#include <stdio.h>
#include <stdlib.h>

static void drop(VM *vm) { int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1); }
static const char *sarg(VM *vm, int i) { Value v = vm_cur_stack(vm)[vm_cur_sp(vm) - i]; return v.type == VAL_STRING && v.sval ? v.sval : ""; }
static int say_console(VM *vm) { const char *s = vm->cur_argc ? sarg(vm, 0) : ""; drop(vm); puts(s); fflush(stdout); return 0; }
static int say_log(VM *vm) { const char *level = vm->cur_argc > 1 ? sarg(vm, 0) : "info"; const char *msg = vm->cur_argc > 1 ? sarg(vm, 1) : (vm->cur_argc ? sarg(vm, 0) : ""); drop(vm); fprintf(stderr, "[%s] %s\n", level, msg); fflush(stderr); return 0; }
static int say_json(VM *vm) { const char *s = vm->cur_argc ? sarg(vm, 0) : "null"; drop(vm); printf("%s\n", s); fflush(stdout); return 0; }
void say_mod_register(VM *vm) {
    vm_register_builtin(vm, "say.console", say_console);
    vm_register_builtin(vm, "say.log", say_log);
    vm_register_builtin(vm, "say.json", say_json);
    vm_register_builtin(vm, "say_console", say_console);
    vm_register_builtin(vm, "say_log", say_log);
    vm_register_builtin(vm, "say_json", say_json);
}
