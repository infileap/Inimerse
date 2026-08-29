#include "vm.h"
#include "say_stream.h"
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
static int say_network(VM *vm) { return say_prefixed(vm, "network"); }
static int say_target(VM *vm) {
    const char *text = vm->cur_argc > 1 ? sarg(vm, 0) : (vm->cur_argc ? sarg(vm, 0) : "");
    const char *target = vm->cur_argc > 1 ? sarg(vm, 1) : "console";
    drop(vm);
    if (!strcmp(target, "console")) { puts(text); fflush(stdout); }
    else if (!strcmp(target, "log")) { fprintf(stderr, "[info] %s\n", text); fflush(stderr); }
    else if (!strcmp(target, "json")) { printf("%s\n", text); fflush(stdout); }
    else { printf("[%s] %s\n", target, text); fflush(stdout); }
    return 0;
}
static int say_file(VM *vm) {
    const char *path = vm->cur_argc > 1 ? sarg(vm, 0) : "";
    const char *msg = vm->cur_argc > 1 ? sarg(vm, 1) : (vm->cur_argc ? sarg(vm, 0) : "");
    int ok = path[0] && !strstr(path, "..") && !strchr(path, '\\') && !strchr(path, ':');
    drop(vm); if (!ok) { push_int(vm, 0); return 1; }
    FILE *f = fopen(path, "ab"); if (!f) { push_int(vm, 0); return 1; }
    fprintf(f, "%s\n", msg); fclose(f); push_int(vm, 1); return 1;
}
static int say_ai_event(VM *vm, const char *kind) {
    const char *payload = vm->cur_argc ? sarg(vm, 0) : "null";
    drop(vm);
    int is_json = payload[0] == '{' || payload[0] == '[' || strcmp(payload, "null") == 0 || strcmp(payload, "true") == 0 || strcmp(payload, "false") == 0;
    printf("{\"source\":\"ai\",\"event\":\"%s\",\"payload\":", kind);
    if (is_json) printf("%s", payload); else { putchar('"'); for (const unsigned char *p = (const unsigned char *)payload; *p; ++p) { if (*p == '"' || *p == '\\') putchar('\\'); putchar(*p); } putchar('"'); }
    printf("}\n");
    fflush(stdout); return 0;
}
static int say_ai(VM *vm) { return say_ai_event(vm, "event"); }
static int say_ai_observe(VM *vm) { return say_ai_event(vm, "observe"); }
static int say_ai_trace(VM *vm) { return say_ai_event(vm, "trace"); }
static int say_ai_feedback(VM *vm) { return say_ai_event(vm, "feedback"); }
void say_mod_register(VM *vm) {
    say_stream_register(vm);
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
    vm_register_builtin(vm, "say_network", say_network);
    vm_register_builtin(vm, "say.network", say_network);
    vm_register_builtin(vm, "say.file", say_file);
    vm_register_builtin(vm, "say_target", say_target);
    vm_register_builtin(vm, "gui_say", say_console);
    vm_register_builtin(vm, "say_ai", say_ai);
    vm_register_builtin(vm, "say_ai_observe", say_ai_observe);
    vm_register_builtin(vm, "say_ai_trace", say_ai_trace);
    vm_register_builtin(vm, "say_ai_feedback", say_ai_feedback);
    vm_register_builtin(vm, "say.ai", say_ai);
    vm_register_builtin(vm, "say.ai_observe", say_ai_observe);
    vm_register_builtin(vm, "say.ai_trace", say_ai_trace);
    vm_register_builtin(vm, "say.ai_feedback", say_ai_feedback);
}
