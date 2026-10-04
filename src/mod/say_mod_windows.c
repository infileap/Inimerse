#include "vm.h"
#include "say_stream.h"
#include <stdio.h>
#include <string.h>

static void drop(VM *vm) { int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1); }
static const char *arg(VM *vm) { if (vm->cur_argc <= 0 || vm_cur_sp(vm) < 0) return ""; Value v = vm_cur_stack(vm)[vm_cur_sp(vm)]; return v.type == VAL_STRING && v.sval ? v.sval : ""; }
/* sarg(vm, 0) is the LAST argument and sarg(vm, 1) the one before it, exactly as
   in src/mod/say_mod_posix.c:9.  The two files register the same builtin names,
   so whichever one the platform compiles defines the contract, and the two must
   read their arguments the same way round.  Three of them did not: say_log,
   say_file and say_target each took the first argument where the other copy
   took the last.  See the note above say_log. */
static const char *sarg(VM *vm, int i) { if (vm_cur_sp(vm) - i < 0) return ""; Value v = vm_cur_stack(vm)[vm_cur_sp(vm) - i]; return v.type == VAL_STRING && v.sval ? v.sval : ""; }
static int say_console(VM *vm) { const char *s = arg(vm); drop(vm); puts(s); fflush(stdout); return 0; }
static int say_prefixed(VM *vm, const char *target) { const char *s = arg(vm); drop(vm); printf("[%s] %s\n", target, s); fflush(stdout); return 0; }
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
#define TARGET_FN(name, label) static int name(VM *vm) { return say_prefixed(vm, label); }
static int say_log(VM *vm) {
    /* say.log(text, level): the first argument is the text and the second the
     * level, so the level is the LAST argument (docs/STATUS.md:441 and
     * src/mod/say_mod_posix.c:11 both say so).  This copy read them the other
     * way round, so say_log("a", "warn") printed "[a] warn" -- the text as the
     * level and the level as the text -- on Windows only. */
    const char *level = vm->cur_argc > 1 ? sarg(vm, 0) : "info";
    const char *msg = vm->cur_argc > 1 ? sarg(vm, 1) : (vm->cur_argc ? sarg(vm, 0) : "");
    drop(vm); fprintf(stderr, "[%s] %s\n", level, msg); fflush(stderr); return 0;
}
TARGET_FN(say_chat, "chat") TARGET_FN(say_ui, "ui") TARGET_FN(say_world, "world")
TARGET_FN(say_character, "character") TARGET_FN(say_dialogue, "dialogue") TARGET_FN(say_system, "system")
TARGET_FN(say_network, "network")
static int say_json(VM *vm) { const char *s = vm->cur_argc ? sarg(vm, 0) : "null"; drop(vm); printf("%s\n", s); fflush(stdout); return 0; }
static int say_ai_event(VM *vm, const char *kind) {
    /* A payload that is already JSON is emitted as JSON; only a bare string is
     * quoted and escaped.  This copy always quoted, so say_ai("{\"a\":1}")
     * answered "\"{\\\"a\\\":1}\"" -- a JSON string containing JSON, which the
     * reader on the other end sees as a string, not as an object.
     * src/mod/say_mod_posix.c:39-46 has the rule. */
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
static int say_file(VM *vm) {
    /* say.file(text, path): text first, path last -- the same order
     * src/mod/say_mod_posix.c:31-38 uses.  This copy had them swapped, so
     * say_file("hello", "out.txt") wrote the path into the file named after the
     * text and reported failure for the write it should have done. */
    const char *path = vm->cur_argc > 1 ? sarg(vm, 0) : "";
    const char *msg = vm->cur_argc > 1 ? sarg(vm, 1) : (vm->cur_argc ? sarg(vm, 0) : "");
    int safe = path[0] && !strstr(path, "..") && !strchr(path, '\\') && !strchr(path, ':');
    drop(vm); if (!safe) { push_int(vm, 0); return 1; }
    FILE *f = fopen(path, "ab"); if (!f) { push_int(vm, 0); return 1; }
    fprintf(f, "%s\n", msg); fclose(f); push_int(vm, 1); return 1;
}
void say_mod_register(VM *vm) {
    say_stream_register(vm);
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
    vm_register_builtin(vm, "say.ai_observe", say_ai_observe); vm_register_builtin(vm, "say_ai_observe", say_ai_observe);
    vm_register_builtin(vm, "say.ai_trace", say_ai_trace); vm_register_builtin(vm, "say_ai_trace", say_ai_trace);
    vm_register_builtin(vm, "say.ai_feedback", say_ai_feedback); vm_register_builtin(vm, "say_ai_feedback", say_ai_feedback);
    vm_register_builtin(vm, "say.network", say_network); vm_register_builtin(vm, "say_network", say_network);
    vm_register_builtin(vm, "say.file", say_file); vm_register_builtin(vm, "say_file", say_file);
}
