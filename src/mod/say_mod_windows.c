#include "vm.h"
#include <stdio.h>
#include <string.h>

static void drop(VM *vm) { int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1); }
static const char *arg(VM *vm) { if (vm->cur_argc <= 0 || vm_cur_sp(vm) < 0) return ""; Value v = vm_cur_stack(vm)[vm_cur_sp(vm)]; return v.type == VAL_STRING && v.sval ? v.sval : ""; }
static int say_console(VM *vm) { const char *s = arg(vm); drop(vm); puts(s); fflush(stdout); return 0; }
static int say_prefixed(VM *vm, const char *target) { const char *s = arg(vm); drop(vm); printf("[%s] %s\n", target, s); fflush(stdout); return 0; }
static int say_target(VM *vm) {
    const char *text = arg(vm); const char *target = "console";
    if (vm->cur_argc > 1 && vm_cur_sp(vm) >= 1) { Value t = vm_cur_stack(vm)[vm_cur_sp(vm) - 1]; if (t.type == VAL_STRING && t.sval && *t.sval) target = t.sval; }
    drop(vm); printf("[%s] %s\n", target, text); fflush(stdout); return 0;
}
#define TARGET_FN(name, label) static int name(VM *vm) { return say_prefixed(vm, label); }
TARGET_FN(say_log, "log") TARGET_FN(say_chat, "chat") TARGET_FN(say_ui, "ui") TARGET_FN(say_world, "world")
TARGET_FN(say_character, "character") TARGET_FN(say_dialogue, "dialogue") TARGET_FN(say_system, "system")
TARGET_FN(say_json, "json") TARGET_FN(say_ai, "ai") TARGET_FN(say_network, "network")
static int say_file(VM *vm) {
    const char *msg = arg(vm); const char *path = (vm->cur_argc > 1 && vm_cur_sp(vm) >= 1) ? (vm_cur_stack(vm)[vm_cur_sp(vm)-1].sval ?: "") : "";
    int safe = path[0] && !strstr(path, "..") && !strchr(path, ':') && !strchr(path, '\\') && !strchr(path, '/');
    drop(vm); if (!safe) { push_int(vm, 0); return 1; }
    FILE *f = fopen(path, "ab"); if (!f) { push_int(vm, 0); return 1; }
    fprintf(f, "%s\n", msg); fclose(f); push_int(vm, 1); return 1;
}
void say_mod_register(VM *vm) {
    vm_register_builtin(vm, "say.console", say_console); vm_register_builtin(vm, "say_console", say_console);
    vm_register_builtin(vm, "say_target", say_target); vm_register_builtin(vm, "gui_say", say_console);
    vm_register_builtin(vm, "say.log", say_log); vm_register_builtin(vm, "say_log", say_log);
    vm_register_builtin(vm, "say.chat", say_chat); vm_register_builtin(vm, "say_chat", say_chat);
    vm_register_builtin(vm, "say.ui", say_ui); vm_register_builtin(vm, "say_ui", say_ui);
    vm_register_builtin(vm, "say.world", say_world); vm_register_builtin(vm, "say_world", say_world);
    vm_register_builtin(vm, "say.character", say_character); vm_register_builtin(vm, "say_character", say_character);
    vm_register_builtin(vm, "say.dialogue", say_dialogue); vm_register_builtin(vm, "say_dialogue", say_dialogue);
    vm_register_builtin(vm, "say.system", say_system); vm_register_builtin(vm, "say_system", say_system);
    vm_register_builtin(vm, "say.json", say_json); vm_register_builtin(vm, "say_json", say_json);
    vm_register_builtin(vm, "say.ai", say_ai); vm_register_builtin(vm, "say_ai", say_ai);
    vm_register_builtin(vm, "say.network", say_network); vm_register_builtin(vm, "say_network", say_network);
    vm_register_builtin(vm, "say.file", say_file); vm_register_builtin(vm, "say_file", say_file);
}
