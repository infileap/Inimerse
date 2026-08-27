#include "vm.h"
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void drop(VM *vm) { int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1); }
static const char *sarg(VM *vm, int i) { Value v = vm_cur_stack(vm)[vm_cur_sp(vm) - i]; return v.type == VAL_STRING && v.sval ? v.sval : ""; }
static int say_console(VM *vm) { const char *s = vm->cur_argc ? sarg(vm, 0) : ""; drop(vm); puts(s); fflush(stdout); return 0; }
static int say_log(VM *vm) { const char *level = vm->cur_argc > 1 ? sarg(vm, 0) : "info"; const char *msg = vm->cur_argc > 1 ? sarg(vm, 1) : (vm->cur_argc ? sarg(vm, 0) : ""); drop(vm); fprintf(stderr, "[%s] %s\n", level, msg); fflush(stderr); return 0; }
static int say_json(VM *vm) { const char *s = vm->cur_argc ? sarg(vm, 0) : "null"; drop(vm); printf("%s\n", s); fflush(stdout); return 0; }
static int say_prefixed(VM *vm, const char *prefix) { const char *s = vm->cur_argc ? sarg(vm, 0) : ""; drop(vm); printf("[%s] %s\n", prefix, s); fflush(stdout); return 0; }
static int say_chat(VM *vm) { return say_prefixed(vm, "chat"); }
static int say_ui(VM *vm) { return say_prefixed(vm, "ui"); }
static int say_world(VM *vm) { return say_prefixed(vm, "world"); }
static int say_character(VM *vm) { return say_prefixed(vm, "character"); }
static int say_dialogue(VM *vm) { return say_prefixed(vm, "dialogue"); }
static int say_system(VM *vm) { return say_prefixed(vm, "system"); }
static int say_file(VM *vm) {
    const char *path = vm->cur_argc > 1 ? sarg(vm, 0) : "";
    const char *msg = vm->cur_argc > 1 ? sarg(vm, 1) : (vm->cur_argc ? sarg(vm, 0) : "");
    int ok = path[0] && !strstr(path, "..") && !strchr(path, '\\') && !strchr(path, ':');
    drop(vm); if (!ok) { push_int(vm, 0); return 1; }
    FILE *f = fopen(path, "ab"); if (!f) { push_int(vm, 0); return 1; }
    fprintf(f, "%s\n", msg); fclose(f); push_int(vm, 1); return 1;
}
void say_mod_register(VM *vm) {
    vm_register_builtin(vm, "say.console", say_console);
    vm_register_builtin(vm, "say.log", say_log);
    vm_register_builtin(vm, "say.json", say_json);
    vm_register_builtin(vm, "say_console", say_console);
    vm_register_builtin(vm, "say_log", say_log);
    vm_register_builtin(vm, "say_json", say_json);
    vm_register_builtin(vm, "say_chat", say_chat);
    vm_register_builtin(vm, "say_ui", say_ui);
    vm_register_builtin(vm, "say_world", say_world);
    vm_register_builtin(vm, "say_character", say_character);
    vm_register_builtin(vm, "say_dialogue", say_dialogue);
    vm_register_builtin(vm, "say_system", say_system);
    vm_register_builtin(vm, "say_file", say_file);
    vm_register_builtin(vm, "say.chat", say_chat);
    vm_register_builtin(vm, "say.ui", say_ui);
    vm_register_builtin(vm, "say.world", say_world);
    vm_register_builtin(vm, "say.character", say_character);
    vm_register_builtin(vm, "say.dialogue", say_dialogue);
    vm_register_builtin(vm, "say.system", say_system);
    vm_register_builtin(vm, "say.file", say_file);
}
