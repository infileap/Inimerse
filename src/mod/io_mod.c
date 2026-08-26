/* ============================================================
 * io_mod.c - I/OÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ£ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ©: ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¼ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¾/ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¿ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ??ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂµÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ³ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¼ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ®/ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¸ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ§HTTP/ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ´ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ®ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¿ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ/ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¼ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¼ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ³ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ£ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¢
 * ??io_mod_register(vm) ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¢ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ²ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¡; runtime.c ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ²ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ»ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¢ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ²ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¡ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¬ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ»ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ
 * ============================================================ */
#include "vm.h"
#include "platform/platform.h"
#include "platform/dir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <conio.h>
#include <winhttp.h>
#include "child_proc.h"
/* forward declarations for extension functions (defined later) */
static int builtin_copy_file(VM *vm);
static int builtin_str2int(VM *vm);
static int builtin_clipboard_set(VM *vm);
static int builtin_clipboard_get(VM *vm);
static int builtin_timer_ms(VM *vm);
static int builtin_exec_async(VM *vm);
static int builtin_proc_list(VM *vm);
static int builtin_proc_kill(VM *vm);
static int builtin_proc_kill_tag(VM *vm);
static int builtin_proc_alive(VM *vm);
static int builtin_proc_prune(VM *vm);
static int builtin_ai_ask(VM *vm);
static int builtin_ai_text(VM *vm);
static int builtin_ai_vision(VM *vm);
static int builtin_ai_models(VM *vm);
static int builtin_ai_status(VM *vm);
static int builtin_ai_busy(VM *vm);
static int builtin_ai_lock(VM *vm);
static int builtin_ai_unlock(VM *vm);
static int builtin_ai_start(VM *vm);
static int builtin_ai_done(VM *vm);
static int builtin_ai_result(VM *vm);
static int builtin_ai_progress(VM *vm);
static int builtin_ai_cancel(VM *vm);
static int builtin_ai_wait_task(VM *vm);
static int builtin_ai_code(VM *vm);


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

static Value io_arg(VM *vm, int i) { return vm_cur_stack(vm)[vm_cur_sp(vm) - i]; }
static const char *io_arg_str(VM *vm, int i) { Value v = io_arg(vm, i); return (v.type == VAL_STRING && v.sval) ? v.sval : ""; }
static double io_arg_num(VM *vm, int i) { Value v = io_arg(vm, i); if (v.type == VAL_INT) return (double)v.ival; if (v.type == VAL_FLOAT) return v.fval; return 0.0; }
static void io_popn(VM *vm, int n) { vm_cur_set_sp(vm, vm_cur_sp(vm) - n); }
static void io_push_int(VM *vm, int v) { push_int(vm, v); }

/* file_exists(path) -> 1/0 */
static int builtin_file_exists(VM *vm) {
    const char *path = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    FILE *f = fopen(path, "rb");
    int ok = f ? 1 : 0;
    if (f) fclose(f);
    push_int(vm, ok);
    return 1;
}

static int builtin_read_file(VM *vm) {
    const char *fn = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    FILE *f = fopen(fn, "rb");
    if (!f) { push_string(vm, ""); return 1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    size_t rd = fread(buf, 1, len, f);
    buf[rd] = '\0';
    fclose(f);
    push_string(vm, buf);
    free(buf);
    return 1;
}
static int builtin_mkdir(VM *vm) {
    const char *path = io_arg_str(vm, vm->cur_argc - 1);
    io_popn(vm, vm->cur_argc);
    int ok = (im_platform_mkdirs(path) == 0) ? 1 : 0;
    push_int(vm, ok);
    return 1;
}

static int builtin_write_file(VM *vm) {
    const char *fn = io_arg_str(vm, 1);
    const char *content = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    FILE *f = fopen(fn, "wb");
    if (!f) { push_int(vm, 0); return 1; }
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    push_int(vm, 1);
    return 1;
}

static int builtin_stdin_ready(VM *vm) {
    int ready = 0;
    if (_kbhit()) ready = 1;
    else {
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (!GetConsoleMode(h, &mode)) {  /* pipe/file, not a console */
                DWORD total = 0;
                if (PeekNamedPipe(h, NULL, 0, NULL, &total, NULL) && total > 0) ready = 1;
            }
        }
    }
    push_int(vm, ready);
    return 1;
}

static int builtin_input(VM *vm) {
    if (vm_cur_sp(vm) >= 0) {
        Value v = io_arg(vm, 0);
        if (v.type == VAL_STRING && v.sval) fputs(v.sval, stdout), fflush(stdout);
    }
    io_popn(vm, vm->cur_argc);
    char buf[4096];
    if (fgets(buf, sizeof(buf), stdin)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
        push_string(vm, buf);
    } else push_string(vm, "");
    return 1;
}
static int builtin_exec(VM *vm) {
    const char *cmd = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    char out[65536] = {0};
    FILE *f = _popen(cmd, "r");
    if (f) {
        size_t total = 0, rd;
        while (total < sizeof(out) - 1 && (rd = fread(out + total, 1, 4096, f)) > 0) total += rd;
        out[total] = '\0';
        _pclose(f);
    }
    push_string(vm, out);
    return 1;
}
static int builtin_random(VM *vm) {
    io_popn(vm, vm->cur_argc);
    push_int(vm, rand());
    return 1;
}
/* io_list_dir(path) -> ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¿ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¼ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ(ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ»ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ»ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ·ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¸ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ´; ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¿ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¼ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ / ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ½ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¡ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ²; ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¿ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ´ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ®=ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ§ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ°ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ) */
static int builtin_io_list_dir(VM *vm) {
    const char *dir = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    char out[65536] = {0};
    size_t total = 0;
    ImDir *handle = im_dir_open(dir);
    char name[1024];
    int isDir = 0;
    if (handle) {
        while (im_dir_next_ex(handle, name, sizeof name, &isDir)) {
            int n = snprintf(out + total, sizeof out - total, "%s%s%s",
                             total ? "\n" : "", name, isDir ? "/" : "");
            if (n < 0 || (size_t)n >= sizeof out - total) break;
            total += (size_t)n;
        }
        im_dir_close(handle);
    }    push_string(vm, out);
    return 1;
}

/* ---------- HTTP(WinHTTP) ---------- */
static int http_request(VM *vm, LPCWSTR verb, const char *url, const char *postdata) {
    char out[65536] = {0};
    HINTERNET hI = WinHttpOpen(L"Inimerse", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (hI) {
        URL_COMPONENTS uc = {0}; uc.dwStructSize = sizeof(uc);
        WCHAR host[256] = {0}, path[2048] = {0};
        uc.lpszHostName = host; uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
        WCHAR wurl[4096]; MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 4096);
        if (WinHttpCrackUrl(wurl, 0, 0, &uc)) {
            HINTERNET hC = WinHttpConnect(hI, host, uc.nPort, 0);
            if (hC) {
                HINTERNET hR = WinHttpOpenRequest(hC, verb, uc.lpszUrlPath ? uc.lpszUrlPath : L"/", NULL, NULL, NULL, 0);
                if (hR) {
                    void *body = NULL; DWORD blen = 0;
                    if (postdata) { blen = (DWORD)strlen(postdata); body = (void*)postdata; }
                    if (WinHttpSendRequest(hR, NULL, 0, body, blen, blen, 0)) {
                        if (WinHttpReceiveResponse(hR, NULL)) {
                            DWORD avail = 0, total = 0;
                            while (WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
                                char buf[8192]; DWORD rd = 0;
                                if (!WinHttpReadData(hR, buf, avail < 8192 ? avail : 8192, &rd)) break;
                                if (rd + total < sizeof(out)) { memcpy(out + total, buf, rd); total += rd; }
                                else break;
                            }
                            out[total] = '\0';
                        }
                    }
                    WinHttpCloseHandle(hR);
                }
                WinHttpCloseHandle(hC);
            }
        }
        WinHttpCloseHandle(hI);
    }
    push_string(vm, out);
    return 1;
}
static int builtin_http_get(VM *vm) {
    const char *url = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    return http_request(vm, L"GET", url, NULL);
}
static int builtin_http_post(VM *vm) {
    const char *url = io_arg_str(vm, 1);
    const char *data = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    return http_request(vm, L"POST", url, data);
}

/* ---------- ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ´ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ®ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¿ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ ---------- */
static int builtin_serial_open(VM *vm) {
    int baud = (int)io_arg_num(vm, 0);
    const char *port = io_arg_str(vm, 1);
    io_popn(vm, vm->cur_argc);
    WCHAR wport[64]; MultiByteToWideChar(CP_ACP, 0, port, -1, wport, 64);
    HANDLE h = CreateFileW(wport, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { push_int(vm, -1); return 1; }
    DCB dcb = {0}; dcb.DCBlength = sizeof(DCB);
    GetCommState(h, &dcb);
    dcb.BaudRate = baud; dcb.ByteSize = 8; dcb.StopBits = ONESTOPBIT; dcb.Parity = NOPARITY;
    SetCommState(h, &dcb);
    COMMTIMEOUTS to = {0}; to.ReadIntervalTimeout = 50; to.ReadTotalTimeoutMultiplier = 10; to.ReadTotalTimeoutConstant = 100;
    SetCommTimeouts(h, &to);
    push_int(vm, (int)(intptr_t)h);
    return 1;
}
static int builtin_serial_write(VM *vm) {
    HANDLE h = (HANDLE)(intptr_t)(int)io_arg_num(vm, 1);
    const char *data = io_arg_str(vm, 0);
    DWORD written = 0;
    WriteFile(h, data, (DWORD)strlen(data), &written, NULL);
    io_popn(vm, vm->cur_argc);
    push_int(vm, (int)written);
    return 1;
}
static int builtin_serial_read(VM *vm) {
    HANDLE h = (HANDLE)(intptr_t)(int)io_arg_num(vm, 1);
    int maxlen = (int)io_arg_num(vm, 0);
    if (maxlen < 1) maxlen = 1;
    if (maxlen > 65536) maxlen = 65536;
    char *buf = malloc(maxlen + 1);
    DWORD rd = 0;
    ReadFile(h, buf, maxlen, &rd, NULL);
    buf[rd] = '\0';
    io_popn(vm, vm->cur_argc);
    push_string(vm, buf);
    free(buf);
    return 1;
}
static int builtin_serial_close(VM *vm) {
    HANDLE h = (HANDLE)(intptr_t)(int)io_arg_num(vm, 0);
    CloseHandle(h);
    io_popn(vm, vm->cur_argc);
    push_int(vm, 1);
    return 1;
}

/* ---------- ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¼ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¼ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ³ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ£ÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂÃÂ¢ ---------- */
static int io_key_vk(const char *key) {
    if (!key) return 0;
    if (key[0] && key[1] == '\0') { if (key[0] >= 'a' && key[0] <= 'z') return key[0] - 32; return key[0]; }
    struct { const char *n; int vk; } map[] = {
        {"enter",VK_RETURN},{"space",VK_SPACE},{"tab",VK_TAB},{"esc",VK_ESCAPE},
        {"backspace",VK_BACK},{"delete",VK_DELETE},{"up",VK_UP},{"down",VK_DOWN},
        {"left",VK_LEFT},{"right",VK_RIGHT},{"ctrl",VK_CONTROL},{"alt",VK_MENU},
        {"shift",VK_SHIFT},{"home",VK_HOME},{"end",VK_END},{"pgup",VK_PRIOR},
        {"pgdn",VK_NEXT},{"f1",VK_F1},{"f2",VK_F2},{"f3",VK_F3},{"f4",VK_F4},
        {"f5",VK_F5},{"f6",VK_F6},{"f7",VK_F7},{"f8",VK_F8},{"f9",VK_F9},
        {"f10",VK_F10},{"f11",VK_F11},{"f12",VK_F12},{"win",VK_LWIN},
        {"caps",VK_CAPITAL},{"insert",VK_INSERT},{NULL,0}
    };
    for (int i = 0; map[i].n; i++) if (strcmp(map[i].n, key) == 0) return map[i].vk;
    return 0;
}
static int builtin_key_press(VM *vm) {
    int vk = io_key_vk(io_arg_str(vm, 0));
    io_popn(vm, vm->cur_argc);
    if (vk) {
        INPUT in = {0}; in.type = INPUT_KEYBOARD; in.ki.wVk = vk;
        SendInput(1, &in, sizeof(in));
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(in));
    }
    push_int(vm, vk ? 1 : 0);
    return 1;
}
static int builtin_mouse_move(VM *vm) {
    int x = (int)io_arg_num(vm, 1), y = (int)io_arg_num(vm, 0);
    SetCursorPos(x, y);
    io_popn(vm, vm->cur_argc);
    push_int(vm, 1);
    return 1;
}
static int builtin_mouse_click(VM *vm) {
    const char *btn = io_arg_str(vm, 0);
    DWORD dw = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP;
    if (strcmp(btn, "right") == 0) dw = MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP;
    else if (strcmp(btn, "middle") == 0) dw = MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP;
    mouse_event(dw, 0, 0, 0, 0);
    io_popn(vm, vm->cur_argc);
    push_int(vm, 1);
    return 1;
}

void io_mod_register(VM *vm) {
    vm_register_builtin_full(vm, "read_file", builtin_read_file, 1|CAP_IO, 0);
    vm_register_builtin(vm, "file_exists", builtin_file_exists);
    vm_register_builtin(vm, "mkdir", builtin_mkdir);
vm_register_builtin_full(vm, "write_file", builtin_write_file, 1|CAP_IO, 0);
    vm_register_builtin(vm, "input", builtin_input);
    vm_register_builtin_full(vm, "exec", builtin_exec, 1|CAP_PROC, 0);
    vm_register_builtin(vm, "random", builtin_random);
    vm_register_builtin(vm, "io_list_dir", builtin_io_list_dir);
    vm_register_builtin(vm, "http_get", builtin_http_get);
    vm_register_builtin(vm, "http_post", builtin_http_post);
    vm_register_builtin(vm, "serial_open", builtin_serial_open);
    vm_register_builtin(vm, "serial_write", builtin_serial_write);
    vm_register_builtin(vm, "serial_read", builtin_serial_read);
    vm_register_builtin(vm, "serial_close", builtin_serial_close);
    vm_register_builtin(vm, "key_press", builtin_key_press);
    vm_register_builtin(vm, "mouse_move", builtin_mouse_move);
    vm_register_builtin(vm, "mouse_click", builtin_mouse_click);
    vm_register_builtin(vm, "copy_file", builtin_copy_file);
    vm_register_builtin(vm, "str2int", builtin_str2int);
    vm_register_builtin(vm, "clipboard_set", builtin_clipboard_set);
    vm_register_builtin(vm, "clipboard_get", builtin_clipboard_get);
    vm_register_builtin(vm, "timer_ms", builtin_timer_ms);
    vm_register_builtin(vm, "exec_async", builtin_exec_async);
    vm_register_builtin(vm, "proc_list", builtin_proc_list);
    vm_register_builtin_full(vm, "proc_kill", builtin_proc_kill, 1|CAP_PROC, 0);
    vm_register_builtin_full(vm, "proc_kill_tag", builtin_proc_kill_tag, 1|CAP_PROC, 0);
    vm_register_builtin(vm, "proc_alive", builtin_proc_alive);
    vm_register_builtin_full(vm, "proc_prune", builtin_proc_prune, 1|CAP_PROC, 0);
    vm_register_builtin_full(vm, "ai_ask", builtin_ai_ask, 1|CAP_AI, 0);
    vm_register_builtin_full(vm, "ai_text", builtin_ai_text, 1|CAP_AI, 0);
    vm_register_builtin_full(vm, "ai_vision", builtin_ai_vision, 1|CAP_AI, 0);
    vm_register_builtin_full(vm, "ai_models", builtin_ai_models, 1|CAP_AI, 0);
    vm_register_builtin(vm, "ai_status", builtin_ai_status);
    vm_register_builtin(vm, "ai_busy", builtin_ai_busy);
    vm_register_builtin(vm, "ai_lock", builtin_ai_lock);
    vm_register_builtin(vm, "ai_unlock", builtin_ai_unlock);
    vm_register_builtin(vm, "ai_start", builtin_ai_start);
    vm_register_builtin(vm, "ai_done", builtin_ai_done);
    vm_register_builtin(vm, "ai_result", builtin_ai_result);
    vm_register_builtin(vm, "ai_progress", builtin_ai_progress);
    vm_register_builtin(vm, "ai_cancel", builtin_ai_cancel);
    vm_register_builtin(vm, "ai_wait_task", builtin_ai_wait_task);
    vm_register_builtin(vm, "ai_code", builtin_ai_code);
    vm_register_builtin(vm, "stdin_ready", builtin_stdin_ready);
}



/* ===== extended: copy_file / str2int / clipboard / timer / child proc ===== */
static int builtin_copy_file(VM *vm) {
    const char *src = io_arg_str(vm, 1);
    const char *dst = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    push_int(vm, CopyFileA(src, dst, FALSE) ? 1 : 0);
    return 1;
}
static int builtin_str2int(VM *vm) {
    const char *s = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    long v = 0;
    if (s && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) v = strtol(s + 2, NULL, 16);
    else v = strtol(s ? s : "", NULL, 10);
    push_int(vm, (int)v);
    return 1;
}
static int builtin_clipboard_set(VM *vm) {
    const char *s = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    int ok = 0;
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        int wlen = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (wlen) * sizeof(WCHAR));
        if (hg) {
            WCHAR *wstr = (WCHAR*)GlobalLock(hg);
            MultiByteToWideChar(CP_ACP, 0, s, -1, wstr, wlen);
            GlobalUnlock(hg);
            ok = SetClipboardData(CF_UNICODETEXT, hg) ? 1 : 0;
            if (!ok) GlobalFree(hg);
        }
        CloseClipboard();
    }
    push_int(vm, ok);
    return 1;
}
static int builtin_clipboard_get(VM *vm) {
    io_popn(vm, vm->cur_argc);
    char out[16384] = {0};
    if (OpenClipboard(NULL)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            WCHAR *w = (WCHAR*)GlobalLock(h);
            if (w) {
                WideCharToMultiByte(CP_ACP, 0, w, -1, out, sizeof(out) - 1, NULL, NULL);
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
    }
    push_string(vm, out);
    return 1;
}
static int builtin_timer_ms(VM *vm) {
    io_popn(vm, vm->cur_argc);
    push_int(vm, (int)GetTickCount());
    return 1;
}
static int builtin_exec_async(VM *vm) {
    const char *cmd = io_arg_str(vm, 1);
    const char *tag = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    push_int(vm, (int)child_proc_spawn(cmd, tag, 1));
    return 1;
}
static int builtin_proc_list(VM *vm) {
    io_popn(vm, vm->cur_argc);
    char *s = child_proc_list();
    push_string(vm, s ? s : "");
    if (s) free(s);
    return 1;
}
static int builtin_proc_kill(VM *vm) {
    int pid = (int)io_arg_num(vm, 0);
    io_popn(vm, vm->cur_argc);
    push_int(vm, child_proc_kill((DWORD)pid));
    return 1;
}
static int builtin_proc_kill_tag(VM *vm) {
    const char *tag = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    push_int(vm, child_proc_kill_tag(tag));
    return 1;
}
static int builtin_proc_alive(VM *vm) {
    int pid = (int)io_arg_num(vm, 0);
    io_popn(vm, vm->cur_argc);
    push_int(vm, child_proc_is_alive((DWORD)pid));
    return 1;
}
static int builtin_proc_prune(VM *vm) {
    io_popn(vm, vm->cur_argc);
    child_proc_prune();
    push_int(vm, 1);
    return 1;
}


/* ===== AI (OLLAMA local models) ===== */
static volatile int g_ai_busy = 0;
static volatile int g_ai_locked = 0;
static char g_ai_last_err[512] = {0};
static unsigned long g_ai_last_ms = 0;
static int g_ai_req_count = 0;

static int ai_b64_encode_file(const char *path, char *out, int cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 40 * 1024 * 1024) { fclose(f); return -1; }
    unsigned char *raw = (unsigned char*)malloc((size_t)sz);
    if (!raw) { fclose(f); return -1; }
    fread(raw, 1, (size_t)sz, f);
    fclose(f);
    static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (long i = 0; i < sz; i += 3) {
        if (o + 4 >= cap) break;
        unsigned v = raw[i] << 16;
        if (i + 1 < sz) v |= raw[i + 1] << 8;
        if (i + 2 < sz) v |= raw[i + 2];
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = (i + 1 < sz) ? tbl[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < sz) ? tbl[v & 63] : '=';
    }
    out[o] = '\0';
    free(raw);
    return o;
}

/* JSON-escape a string for embedding in a JSON body */
static void ai_json_escape(const char *src, char *dst, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; src && src[i] && o + 8 < cap; i++) {
        char c = src[i];
        if (c == '"') { dst[o++] = '\\'; dst[o++] = '"'; }
        else if (c == '\\') { dst[o++] = '\\'; dst[o++] = '\\'; }
        else if (c == '\n') { dst[o++] = '\\'; dst[o++] = 'n'; }
        else if (c == '\r') { dst[o++] = '\\'; dst[o++] = 'r'; }
        else if (c == '\t') { dst[o++] = '\\'; dst[o++] = 't'; }
        else if ((unsigned char)c < 0x20) { dst[o++] = ' '; }
        else dst[o++] = c;
    }
    dst[o] = '\0';
}

static int ai_generate(const char *mname, const char *imgpath, const char *prompt, char *result, size_t cap) {
    char body[42000] = {0};
    char img64[65536] = {0};
    char model[256] = {0};
    snprintf(model, sizeof(model), "%s", (mname && mname[0]) ? mname : "Qwen2.5-7B-Instruct");
    int n = 0;
    if (imgpath && imgpath[0]) {
        int blen = ai_b64_encode_file(imgpath, img64, sizeof(img64));
        if (blen < 0) { snprintf(result, cap, "AI:ERR image"); return -1; }
        { char ep[20000]; ai_json_escape(prompt, ep, sizeof(ep)); n = snprintf(body, sizeof(body), "{\"model\":\"%s\",\"images\":[\"%s\"],\"prompt\":\"%s\",\"stream\":false}", model, img64, ep); }
    } else {
        { char ep[20000]; ai_json_escape(prompt, ep, sizeof(ep)); n = snprintf(body, sizeof(body), "{\"model\":\"%s\",\"prompt\":\"%s\",\"stream\":false}", model, ep); }
    }
    if (n <= 0 || (size_t)n >= sizeof(body)) { snprintf(result, cap, "AI:ERR body"); return -1; }
    HINTERNET hI = WinHttpOpen(L"Inimerse-AI", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    int rc = -1;
    if (hI) {
        HINTERNET hC = WinHttpConnect(hI, L"127.0.0.1", 11434, 0);
        if (hC) {
            HINTERNET hR = WinHttpOpenRequest(hC, L"POST", L"/api/generate", NULL, NULL, NULL, 0);
            if (hR) {
                LPCWSTR hdrs = L"Content-Type: application/json";
                if (WinHttpSendRequest(hR, hdrs, (DWORD)-1L, (LPVOID)body, (DWORD)strlen(body), (DWORD)strlen(body), 0)) {
                    if (WinHttpReceiveResponse(hR, NULL)) {
                        DWORD total = 0;
                        char tmp[8192];
                        DWORD avail = 0;
                        while (WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
                            DWORD rd = 0;
                            if (!WinHttpReadData(hR, tmp, avail < 8191 ? avail : 8191, &rd)) break;
                            if (total + rd < cap - 1) { memcpy(result + total, tmp, rd); total += rd; }
                            else break;
                        }
                        result[total] = '\0';
                        rc = 0;
                        char *rp = strstr(result, "\"response\":\"");
                        if (rp) {
                            rp += 12;
                            char *e = strchr(rp, '"');
                            if (e) *e = '\0';
                            memmove(result, rp, strlen(rp) + 1);
                        }
                    }
                }
                WinHttpCloseHandle(hR);
            }
            WinHttpCloseHandle(hC);
        }
        WinHttpCloseHandle(hI);
    }
    if (rc < 0) snprintf(result, cap, "AI:ERR http");
    return rc;
}
static int builtin_ai_ask(VM *vm) {
    const char *model = io_arg_str(vm, 2);
    const char *img = io_arg_str(vm, 1);
    const char *prompt = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    if (g_ai_busy || g_ai_locked) { push_string(vm, "AI:ERR busy"); return 1; }
    g_ai_busy = 1;
    unsigned long t0 = GetTickCount();
    char res[32768] = {0};
    ai_generate(model, img, prompt, res, sizeof(res));
    g_ai_last_ms = GetTickCount() - t0;
    g_ai_busy = 0;
    g_ai_req_count++;
    push_string(vm, res);
    return 1;
}
static int builtin_ai_text(VM *vm) {
    const char *prompt = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    if (g_ai_busy || g_ai_locked) { push_string(vm, "AI:ERR busy"); return 1; }
    g_ai_busy = 1;
    unsigned long t0 = GetTickCount();
    char res[32768] = {0};
    ai_generate("Qwen2.5-7B-Instruct", NULL, prompt, res, sizeof(res));
    g_ai_last_ms = GetTickCount() - t0;
    g_ai_busy = 0;
    g_ai_req_count++;
    push_string(vm, res);
    return 1;
}
static int builtin_ai_vision(VM *vm) {
    const char *img = io_arg_str(vm, 1);
    const char *prompt = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    if (g_ai_busy || g_ai_locked) { push_string(vm, "AI:ERR busy"); return 1; }
    g_ai_busy = 1;
    unsigned long t0 = GetTickCount();
    char res[32768] = {0};
    ai_generate("llava:7b", img, prompt, res, sizeof(res));
    g_ai_last_ms = GetTickCount() - t0;
    g_ai_busy = 0;
    g_ai_req_count++;
    push_string(vm, res);
    return 1;
}
static int builtin_ai_models(VM *vm) {
    io_popn(vm, vm_cur_sp(vm) + 1);
    char res[4096] = {0};
    ai_generate("Qwen2.5-7B-Instruct", NULL, "list", res, sizeof(res));
    push_string(vm, res);
    return 1;
}
static int builtin_ai_status(VM *vm) {
    io_popn(vm, vm_cur_sp(vm) + 1);
    char out[512];
    snprintf(out, sizeof(out), "busy=%d locked=%d reqs=%d lastms=%lu err=%s", g_ai_busy, g_ai_locked, g_ai_req_count, g_ai_last_ms, g_ai_last_err);
    push_string(vm, out);
    return 1;
}
static int builtin_ai_busy(VM *vm) {
    io_popn(vm, vm_cur_sp(vm) + 1);
    push_int(vm, (g_ai_busy || g_ai_locked) ? 1 : 0);
    return 1;
}
static int builtin_ai_lock(VM *vm) {
    io_popn(vm, vm_cur_sp(vm) + 1);
    if (g_ai_busy) { push_int(vm, 0); return 1; }
    g_ai_locked = 1;
    push_int(vm, 1);
    return 1;
}
static int builtin_ai_unlock(VM *vm) {
    io_popn(vm, vm_cur_sp(vm) + 1);
    g_ai_locked = 0;
    push_int(vm, 1);
    return 1;
}
/* async AI task (prevents UI lockup) */
#define AI_TASK_STATE_FREE 0
#define AI_TASK_STATE_RUN   1
#define AI_TASK_STATE_DONE  2
#define AI_TASK_STATE_ERR   3
#define AI_TASK_STATE_CANCEL 4
typedef struct {
    volatile int state;
    char result[32768];
    char err[512];
    unsigned long t0;
    HANDLE hThread;
    char model[128];
    char img[1024];
    char prompt[16384];
} AiTask;
static AiTask g_ai_task;
static int g_ai_task_inited = 0;
static CRITICAL_SECTION g_ai_task_lock;
static DWORD WINAPI ai_task_worker(LPVOID p) {
    AiTask *t = (AiTask*)p;
    char res[32768] = {0};
    int rc = ai_generate(t->model, t->img[0] ? t->img : NULL, t->prompt, res, sizeof(res));
    EnterCriticalSection(&g_ai_task_lock);
    if (t->state == AI_TASK_STATE_CANCEL) { t->state = AI_TASK_STATE_FREE; }
    else if (rc < 0) { snprintf(t->err, sizeof(t->err), "%s", res); t->state = AI_TASK_STATE_ERR; }
    else { snprintf(t->result, sizeof(t->result), "%s", res); t->state = AI_TASK_STATE_DONE; }
    g_ai_busy = 0;
    LeaveCriticalSection(&g_ai_task_lock);
    return 0;
}
static void ai_task_init(void) {
    if (!g_ai_task_inited) { InitializeCriticalSection(&g_ai_task_lock); g_ai_task_inited = 1; }
}
static int builtin_ai_start(VM *vm) {
    const char *prompt = io_arg_str(vm, 2);
    const char *img = io_arg_str(vm, 1);
    const char *model = io_arg_str(vm, 0);
    io_popn(vm, vm->cur_argc);
    ai_task_init();
    if (g_ai_busy || g_ai_locked || g_ai_task.state != AI_TASK_STATE_FREE) { push_int(vm, 0); return 1; }
    EnterCriticalSection(&g_ai_task_lock);
    memset(&g_ai_task.result, 0, sizeof(g_ai_task.result));
    memset(&g_ai_task.err, 0, sizeof(g_ai_task.err));
    snprintf(g_ai_task.model, sizeof(g_ai_task.model), "%s", (model && model[0]) ? model : "Qwen2.5-7B-Instruct");
    snprintf(g_ai_task.img, sizeof(g_ai_task.img), "%s", img ? img : "");
    snprintf(g_ai_task.prompt, sizeof(g_ai_task.prompt), "%s", prompt ? prompt : "");
    g_ai_task.t0 = GetTickCount();
    g_ai_task.state = AI_TASK_STATE_RUN;
    g_ai_busy = 1;
    g_ai_task.hThread = CreateThread(NULL, 0, ai_task_worker, &g_ai_task, 0, NULL);
    if (!g_ai_task.hThread) { g_ai_task.state = AI_TASK_STATE_FREE; g_ai_busy = 0; LeaveCriticalSection(&g_ai_task_lock); push_int(vm, 0); return 1; }
    LeaveCriticalSection(&g_ai_task_lock);
    push_int(vm, 1);
    return 1;
}
static int builtin_ai_done(VM *vm) {
    io_popn(vm, vm_cur_sp(vm) + 1);
    push_int(vm, (g_ai_task.state == AI_TASK_STATE_DONE || g_ai_task.state == AI_TASK_STATE_ERR) ? 1 : 0);
    return 1;
}
static int builtin_ai_result(VM *vm) {
    io_popn(vm, vm_cur_sp(vm) + 1);
    if (g_ai_task.state == AI_TASK_STATE_DONE) { push_string(vm, g_ai_task.result); return 1; }
    if (g_ai_task.state == AI_TASK_STATE_ERR) { push_string(vm, g_ai_task.err); return 1; }
    push_string(vm, "AI:ERR not ready");
    return 1;
}
static int builtin_ai_progress(VM *vm) {
    io_popn(vm, vm_cur_sp(vm) + 1);
    int s = g_ai_task.state;
    if (s == AI_TASK_STATE_FREE) { push_int(vm, 0); return 1; }
    if (s == AI_TASK_STATE_DONE || s == AI_TASK_STATE_ERR) { push_int(vm, 1000); return 1; }
    unsigned long el = GetTickCount() - g_ai_task.t0;
    int p = (int)((el * 1000) / 90000);
    if (p > 950) p = 950;
    push_int(vm, p);
    return 1;
}
static int builtin_ai_cancel(VM *vm) {
    io_popn(vm, vm_cur_sp(vm) + 1);
    if (g_ai_task.state == AI_TASK_STATE_RUN) {
        EnterCriticalSection(&g_ai_task_lock);
        g_ai_task.state = AI_TASK_STATE_CANCEL;
        g_ai_busy = 0;
        LeaveCriticalSection(&g_ai_task_lock);
        push_int(vm, 1);
    } else push_int(vm, 0);
    return 1;
}
static int builtin_ai_wait_task(VM *vm) {
    long ms = (long)io_arg_num(vm, 0);
    io_popn(vm, vm_cur_sp(vm) + 1);
    unsigned long t0 = GetTickCount();
    while (g_ai_task.state == AI_TASK_STATE_RUN) {
        if (ms > 0 && (GetTickCount() - t0) >= (unsigned long)ms) { push_int(vm, 0); return 1; }
        Sleep(50);
    }
    push_int(vm, 1);
    return 1;
}
static int builtin_ai_code(VM *vm) {
    const char *prompt = io_arg_str(vm, 0);
    const char *filepath = io_arg_str(vm, 1);
    const char *model = io_arg_str(vm, 2);
    io_popn(vm, vm->cur_argc);
    if (g_ai_busy || g_ai_locked) { push_string(vm, "AI:ERR busy"); return 1; }
    const char *mname = (model && model[0]) ? model : "Qwen2.5-7B-Instruct";
    FILE *kb = fopen("D:\\inimerse_stable\\_ai_kb.im", "rb");
    char *kbtxt = NULL;
    if (kb) {
        fseek(kb, 0, SEEK_END); long ksz = ftell(kb); fseek(kb, 0, SEEK_SET);
        if (ksz > 0 && ksz < 100000) { kbtxt = (char*)malloc((size_t)ksz + 1); if (kbtxt) { fread(kbtxt, 1, (size_t)ksz, kb); kbtxt[ksz] = '\0'; } }
        fclose(kb);
    }
    char *full = (char*)malloc(65536);
    int n = 0;
    n += snprintf(full + n, 65536 - n, "You are an expert in the Inimerse game language. Follow the spec strictly:\n\n%s\n\n", kbtxt ? kbtxt : "(no spec)");
    n += snprintf(full + n, 65536 - n, "=== USER REQUEST ===\n%s\n\nReturn ONLY the complete new script (or exact changed lines). Respect reserved words and syntax rules.", prompt);
    if (kbtxt) free(kbtxt);
    g_ai_busy = 1;
    unsigned long t0 = GetTickCount();
    char result[32768] = {0};
    int rc = ai_generate(mname, NULL, full, result, sizeof(result));
    g_ai_last_ms = GetTickCount() - t0;
    g_ai_busy = 0;
    g_ai_req_count++;
    free(full);
    if (rc < 0) snprintf(g_ai_last_err, sizeof(g_ai_last_err), "%s", result);
    else g_ai_last_err[0] = '\0';
    push_string(vm, result);
    return 1;
}

#pragma GCC diagnostic pop
