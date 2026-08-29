/* isolate_mod.c - isolated subprocess execution (P2: unsigned-module process isolation)
 *
 * isolate_run(script, timeout_ms) -> dict { exit:int, out:string, timedout:int }
 *
 *   Spawns a fresh inimerse.exe child running `script` with a hard sandbox:
 *     --safe          dangerous builtins (io/net/process/code-injection) blocked
 *     --low-config    64MB mem / 32MB vram / 10s time caps
 *     --time-limit N  VM execution deadline (seconds)
 *   Child stdout+stderr are captured through a pipe (64KB cap).
 *   Timeout terminates the child; the parent process is fully isolated from
 *   child crashes (process boundary) - this is the safety base for node computing.
 *
 * Registered as a dangerous builtin (CAP_PROC): safe-mode parents cannot call it.
 */
#ifdef _WIN32
#include "vm.h"
#include "platform/platform.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ISO_OUT_CAP 65536
#define ISO_READ_CHUNK 4096
#define ISO_DEFAULT_TIMEOUT_MS 5000

typedef struct {
    HANDLE hRead;
    char *buf;
    int len, cap;
} IsoCtx;

static DWORD WINAPI iso_read_thread(LPVOID arg) {
    IsoCtx *c = (IsoCtx *)arg;
    char tmp[ISO_READ_CHUNK];
    DWORD rd;
    for (;;) {
        if (!ReadFile(c->hRead, tmp, sizeof tmp, &rd, NULL) || rd == 0) break;
        if (c->len + (int)rd + 1 > c->cap) {
            int nc = c->cap ? c->cap * 2 : ISO_READ_CHUNK;
            if (nc > ISO_OUT_CAP) nc = ISO_OUT_CAP;
            char *nb = (char *)realloc(c->buf, (size_t)nc);
            if (!nb) break;
            c->buf = nb;
            c->cap = nc;
        }
        int take = (int)rd;
        if (c->len + take + 1 > c->cap) take = c->cap - c->len - 1;
        if (take > 0) { memcpy(c->buf + c->len, tmp, (size_t)take); c->len += take; }
        if (take < (int)rd) break; /* output cap reached: stop collecting */
    }
    return 0;
}

/* arg helpers (io-style: r_arg(0) = last arg) */
static Value iso_arg(VM *vm, int i) { return vm_cur_stack(vm)[vm_cur_sp(vm) - i]; }
static void iso_popn(VM *vm, int n) {
    while (n-- > 0 && vm_cur_sp(vm) >= 0) {
        Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
        if (v.type == VAL_STRING && v.ival != 1 && v.sval) free(v.sval);
        vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    }
}
static void iso_push(VM *vm, Value v) {
    if (vm_cur_sp(vm) < 1023) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = v;
    }
}
static void iso_push_int(VM *vm, int n) {
    Value v; v.type = VAL_INT; v.ival = n; v.fval = 0; v.sval = NULL;
    iso_push(vm, v);
}
static void iso_push_str(VM *vm, const char *s) {
    Value v; v.type = VAL_STRING; v.ival = 0; v.fval = 0; v.sval = strdup(s ? s : "");
    iso_push(vm, v);
}

/* push dict {exit, out, timedout} */
static void iso_push_result(VM *vm, long exit_code, const char *out, int timedout) {
    int aidx = vm_array_new(vm);
    if (aidx < 0) { iso_push_int(vm, -999); return; }
    Value k, val;
    k.type = VAL_STRING; k.ival = 0; k.fval = 0; k.sval = strdup("exit");
    val.type = VAL_INT; val.ival = (int)exit_code; val.fval = 0; val.sval = NULL;
    vm_dict_set(vm, aidx, &k, &val);
    free(k.sval);
    k.sval = strdup("out");
    val.type = VAL_STRING; val.ival = 0; val.fval = 0; val.sval = strdup(out ? out : "");
    vm_dict_set(vm, aidx, &k, &val);
    free(k.sval);
    free(val.sval);
    k.sval = strdup("timedout");
    val.type = VAL_INT; val.ival = timedout; val.fval = 0; val.sval = NULL;
    vm_dict_set(vm, aidx, &k, &val);
    free(k.sval);
    Value dv; dv.type = VAL_DICT; dv.ival = aidx + 1; dv.fval = 0; dv.sval = NULL;
    iso_push(vm, dv);
}

static int fn_isolate_run(VM *vm) {
    int sp0 = vm_cur_sp(vm);
    const char *script = "";
    double timeout_ms = ISO_DEFAULT_TIMEOUT_MS;
    /* r_arg(0) = top of stack = LAST argument (io-style): isolate_run(script, timeout_ms) */
    if (sp0 >= 0) {
        Value a = iso_arg(vm, 0);
        if (a.type == VAL_INT) timeout_ms = (double)a.ival;
        else if (a.type == VAL_FLOAT) timeout_ms = a.fval;
    }
    if (sp0 >= 1) {
        Value s = iso_arg(vm, 1);
        if (s.type == VAL_STRING && s.sval) script = s.sval;
    }
    if (timeout_ms < 100) timeout_ms = 100;
    iso_popn(vm, (sp0 >= 1) ? 2 : 1);

    char exe[1024];
    if (im_platform_executable_path(exe, sizeof exe) < 0 || !exe[0]) {
        iso_push_result(vm, -2, "isolate_run: cannot resolve exe path", 0);
        return 0;
    }

    int tsec = (int)(timeout_ms / 1000.0) + 1;
    if (tsec < 1) tsec = 1;
    char cmd[8192];
    snprintf(cmd, sizeof cmd, "\"%s\" --safe --low-config --time-limit %d \"%s\"", exe, tsec, script);

    HANDLE hRead = NULL, hWrite = NULL;
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        iso_push_result(vm, -2, "isolate_run: CreatePipe failed", 0);
        return 0;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0); /* read end not inherited */

    /* stdin: NUL device so the child never reads parent input */
    HANDLE hNull = CreateFileA("NUL", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = (hNull != INVALID_HANDLE_VALUE) ? hNull : GetStdHandle(STD_INPUT_HANDLE);
    memset(&pi, 0, sizeof pi);
    char *cmdCopy = _strdup(cmd);
    BOOL ok = CreateProcessA(NULL, cmdCopy, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             NULL, NULL, &si, &pi);
    free(cmdCopy);
    if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);
    CloseHandle(hWrite); /* parent keeps only the read end */
    if (!ok) {
        CloseHandle(hRead);
        iso_push_result(vm, -2, "isolate_run: cannot spawn child process", 0);
        return 0;
    }

    /* Put the child in a kill-on-close Job Object.  TerminateProcess only
       covers the immediate VM process; a malicious/buggy module could leave
       descendants alive.  The job gives Windows the same process-tree
       isolation semantics as the POSIX fork boundary. */
    HANDLE hJob = CreateJobObjectA(NULL, NULL);
    if (!hJob) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hRead);
        iso_push_result(vm, -2, "isolate_run: CreateJobObject failed", 0);
        return 0;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    memset(&limits, 0, sizeof limits);
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    /* Keep the hard cap aligned with --low-config.  Failure to set the limit
       is non-fatal on older Windows versions, while assignment remains safe. */
    limits.ProcessMemoryLimit = (SIZE_T)64 * 1024 * 1024;
    limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                            &limits, sizeof limits);
    if (!AssignProcessToJobObject(hJob, pi.hProcess)) {
        CloseHandle(hJob);
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hRead);
        iso_push_result(vm, -2, "isolate_run: AssignProcessToJobObject failed", 0);
        return 0;
    }

    IsoCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.hRead = hRead;
    HANDLE hThread = CreateThread(NULL, 0, iso_read_thread, &ctx, 0, NULL);

    DWORD wait = WaitForSingleObject(pi.hProcess, (DWORD)timeout_ms);
    int timedout = (wait == WAIT_TIMEOUT) ? 1 : 0;
    if (timedout) {
        TerminateJobObject(hJob, 9);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    /* wait for the reader thread first: it exits on pipe EOF once the child is dead.
       Closing hRead while the reader may still be blocked in ReadFile is undefined
       behavior and caused occasional heap corruption (0xC0000374). */
    if (hThread) WaitForSingleObject(hThread, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);
    if (hThread) CloseHandle(hThread);
    CloseHandle(hJob); /* kill-on-close also cleans any descendant handles */

    iso_push_result(vm, (long)exitCode, ctx.buf ? ctx.buf : "", timedout);
    free(ctx.buf);
    return 0;
}

static int b_isolate_run(VM *vm) { return fn_isolate_run(vm); }

void isolate_mod_register(VM *vm) {
    vm_register_builtin_full(vm, "isolate_run", b_isolate_run, 1 | CAP_PROC, 0);
}
#else
#include "vm.h"
#include "platform/platform.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ISO_OUT_CAP 65536
#define ISO_DEFAULT_TIMEOUT_MS 5000

static Value iso_arg(VM *vm, int i) { return vm_cur_stack(vm)[vm_cur_sp(vm) - i]; }
static void iso_popn(VM *vm, int n) {
    while (n-- > 0 && vm_cur_sp(vm) >= 0) {
        Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
        if (v.type == VAL_STRING && v.ival != 1 && v.sval) free(v.sval);
        vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    }
}
static void iso_push(VM *vm, Value v) {
    if (vm_cur_sp(vm) < 1023) { vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = v; }
}
static void iso_push_result(VM *vm, int exit_code, const char *out, int timedout) {
    int aidx = vm_array_new(vm); if (aidx < 0) return;
    Value k, v; k.type = VAL_STRING; k.ival = 0; k.fval = 0; v.sval = NULL;
    k.sval = strdup("exit"); v.type = VAL_INT; v.ival = exit_code; vm_dict_set(vm, aidx, &k, &v); free(k.sval);
    k.sval = strdup("out"); v.type = VAL_STRING; v.sval = strdup(out ? out : ""); vm_dict_set(vm, aidx, &k, &v); free(k.sval); free(v.sval);
    k.sval = strdup("timedout"); v.type = VAL_INT; v.ival = timedout; v.sval = NULL; vm_dict_set(vm, aidx, &k, &v); free(k.sval);
    v.type = VAL_DICT; v.ival = aidx + 1; v.sval = NULL; iso_push(vm, v);
}

/* Quote an argument for /bin/sh without allowing path characters to escape. */
static int iso_shell_quote(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    if (!dst || cap < 3) return -1;
    dst[n++] = '\'';
    for (const unsigned char *p = (const unsigned char *)(src ? src : ""); *p; ++p) {
        if (*p == '\'') {
            if (n + 4 >= cap) return -1;
            dst[n++] = '\''; dst[n++] = '\\'; dst[n++] = '\''; dst[n++] = '\'';
        } else {
            if (n + 1 >= cap) return -1;
            dst[n++] = (char)*p;
        }
    }
    if (n + 2 > cap) return -1;
    dst[n++] = '\''; dst[n] = 0;
    return 0;
}

static int fn_isolate_run(VM *vm) {
    int sp = vm_cur_sp(vm); const char *script = ""; int timeout = ISO_DEFAULT_TIMEOUT_MS;
    if (sp >= 0) { Value v = iso_arg(vm, 0); if (v.type == VAL_INT) timeout = v.ival; else if (v.type == VAL_FLOAT) timeout = (int)v.fval; }
    if (sp >= 1) { Value v = iso_arg(vm, 1); if (v.type == VAL_STRING && v.sval) script = v.sval; }
    if (timeout < 100) timeout = 100;
    iso_popn(vm, sp >= 1 ? 2 : 1);
    char exe[1024]; if (im_platform_executable_path(exe, sizeof exe) != 0 || !exe[0]) { iso_push_result(vm, -2, "isolate_run: cannot resolve exe path", 0); return 0; }
    int pipefd[2]; if (pipe(pipefd) != 0) { iso_push_result(vm, -2, "isolate_run: pipe failed", 0); return 0; }
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); iso_push_result(vm, -2, "isolate_run: fork failed", 0); return 0; }
    if (pid == 0) {
        struct rlimit mem_limit = { 64UL * 1024UL * 1024UL, 64UL * 1024UL * 1024UL };
        (void)setrlimit(RLIMIT_AS, &mem_limit);
        struct rlimit cpu_limit = { (rlim_t)(timeout / 1000 + 2), (rlim_t)(timeout / 1000 + 2) };
        (void)setrlimit(RLIMIT_CPU, &cpu_limit);
        dup2(pipefd[1], STDOUT_FILENO); dup2(pipefd[1], STDERR_FILENO); close(pipefd[0]); close(pipefd[1]);
        char qexe[2048], qscript[8192], cmd[12288];
        if (iso_shell_quote(qexe, sizeof qexe, exe) != 0 || iso_shell_quote(qscript, sizeof qscript, script) != 0) _exit(126);
        snprintf(cmd, sizeof cmd, "%s --safe --low-config --time-limit %d %s", qexe, timeout / 1000 + 1, qscript);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL); _exit(127);
    }
    close(pipefd[1]); fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    char *buf = (char *)calloc(1, ISO_OUT_CAP + 1); int len = 0, done = 0, timedout = 0, status = 0; uint64_t start = im_platform_now_ms();
    while (!done) {
        char tmp[4096]; ssize_t n = read(pipefd[0], tmp, sizeof tmp);
        if (n > 0) { int take = n > ISO_OUT_CAP - len ? ISO_OUT_CAP - len : (int)n; if (take > 0) { memcpy(buf + len, tmp, (size_t)take); len += take; buf[len] = 0; } }
        pid_t wr = waitpid(pid, &status, WNOHANG); if (wr == pid) { done = 1; if (len >= ISO_OUT_CAP) { /* drain is unnecessary after child exit */ } }
        if (!done && im_platform_now_ms() - start >= (uint64_t)timeout) { kill(pid, SIGKILL); waitpid(pid, &status, 0); timedout = 1; done = 1; }
        if (!done) { fd_set rf; FD_ZERO(&rf); FD_SET(pipefd[0], &rf); struct timeval tv = {0, 10000}; select(pipefd[0] + 1, &rf, NULL, NULL, &tv); }
    }
    close(pipefd[0]); int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status); iso_push_result(vm, code, buf, timedout); free(buf); return 0;
}
static int b_isolate_run(VM *vm) { return fn_isolate_run(vm); }
void isolate_mod_register(VM *vm) { vm_register_builtin_full(vm, "isolate_run", b_isolate_run, 1 | CAP_PROC, 0); }
#endif
