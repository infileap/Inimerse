#include "process.h"
#include <stdlib.h>
#include <string.h>

struct ImProcess {
    uint64_t pid;
#ifdef _WIN32
    void *handle;
#else
    int status;
    int finished;
#endif
};

#ifdef _WIN32
#include <windows.h>
ImProcess *im_process_spawn(const char *command, int new_console) {
    if (!command || !*command) return NULL;
    char *cmd = _strdup(command); if (!cmd) return NULL;
    STARTUPINFOA si = {0}; PROCESS_INFORMATION pi = {0}; si.cb = sizeof(si);
    DWORD flags = new_console ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi)) { free(cmd); return NULL; }
    free(cmd); CloseHandle(pi.hThread);
    ImProcess *p = (ImProcess *)calloc(1, sizeof(*p));
    if (!p) { TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hProcess); return NULL; }
    p->pid = pi.dwProcessId; p->handle = pi.hProcess; return p;
}
uint64_t im_process_pid(const ImProcess *p) { return p ? p->pid : 0; }
int im_process_alive(ImProcess *p) { DWORD code = 0; return p && p->handle && GetExitCodeProcess((HANDLE)p->handle, &code) && code == STILL_ACTIVE; }
int im_process_wait(ImProcess *p, unsigned int timeout_ms) { return p && p->handle && WaitForSingleObject((HANDLE)p->handle, timeout_ms) == WAIT_OBJECT_0 ? 0 : -1; }
int im_process_wait_kill(ImProcess *p, unsigned int timeout_ms) { if (!p || !p->handle) return -1; DWORD r = WaitForSingleObject((HANDLE)p->handle, timeout_ms); if (r == WAIT_OBJECT_0) return 0; if (r == WAIT_TIMEOUT) { TerminateProcess((HANDLE)p->handle, 1); WaitForSingleObject((HANDLE)p->handle, INFINITE); return 1; } return -1; }
int im_process_kill(ImProcess *p) { return p && p->handle && TerminateProcess((HANDLE)p->handle, 1) ? 0 : -1; }
int im_process_exit_code(ImProcess *p) { DWORD code = STILL_ACTIVE; return p && p->handle && GetExitCodeProcess((HANDLE)p->handle, &code) && code != STILL_ACTIVE ? (int)code : -1; }
void im_process_close(ImProcess *p) { if (!p) return; if (p->handle) CloseHandle((HANDLE)p->handle); free(p); }
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
ImProcess *im_process_spawn(const char *command, int new_console) {
    (void)new_console; if (!command || !*command) return NULL;
    pid_t pid = fork();
    if (pid < 0) return NULL;
    if (pid == 0) { execl("/bin/sh", "sh", "-c", command, (char *)NULL); _exit(127); }
    ImProcess *p = (ImProcess *)calloc(1, sizeof(*p)); if (!p) { kill(pid, SIGKILL); return NULL; }
    p->pid = (uint64_t)pid; return p;
}
uint64_t im_process_pid(const ImProcess *p) { return p ? p->pid : 0; }
int im_process_alive(ImProcess *p) {
    if (!p || p->finished) return 0;
    int status = 0; pid_t r = waitpid((pid_t)p->pid, &status, WNOHANG);
    if (r == (pid_t)p->pid) { p->status = status; p->finished = 1; return 0; }
    if (r < 0 && errno == EINTR) return 1;
    return r == 0;
}
int im_process_wait(ImProcess *p, unsigned int timeout_ms) {
    if (!p) return -1;
    unsigned int elapsed = 0;
    while (im_process_alive(p)) { if (elapsed >= timeout_ms) return -1; struct timespec ts={0,10000000L}; nanosleep(&ts,NULL); elapsed += 10; }
    return 0;
}
int im_process_wait_kill(ImProcess *p, unsigned int timeout_ms) { if (!p) return -1; if (im_process_wait(p, timeout_ms) == 0) return 0; if (im_process_kill(p) != 0) return -1; (void)im_process_wait(p, 1000); return 1; }
int im_process_kill(ImProcess *p) { return p && !p->finished && kill((pid_t)p->pid, SIGKILL) == 0 ? 0 : -1; }
int im_process_exit_code(ImProcess *p) { if (!p) return -1; if (!p->finished) (void)im_process_alive(p); if (!p->finished) return -1; return WIFEXITED(p->status) ? WEXITSTATUS(p->status) : 128 + WTERMSIG(p->status); }
void im_process_close(ImProcess *p) { free(p); }
#endif
