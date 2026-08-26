/* child_proc.h - child process registry (anti-zombie) */
#ifndef CHILD_PROC_H
#define CHILD_PROC_H

#include <stdint.h>
#include "platform/process.h"
#ifdef _WIN32
#include <windows.h>
#else
typedef uint64_t DWORD;
#endif

#define CHILD_PROC_MAX 64
#define CHILD_CMD_LEN 512

typedef struct {
    DWORD pid;
    ImProcess *process;
    char cmd[CHILD_CMD_LEN];
    char tag[64];
    uint64_t start_ms;
    int in_use;
} ChildProcEntry;

/* registry access (thread-safe via internal critical section) */
void child_proc_init(void);
void child_proc_shutdown(void);
/* spawn a child, register it, return pid (0 on fail). tag copies caller string. */
DWORD child_proc_spawn(const char *cmdline, const char *tag, int new_console);
/* return 1 if pid is alive (0 unknown/exited) */
int child_proc_is_alive(DWORD pid);
/* terminate one pid (1=terminated, 0=not found/failed) */
int child_proc_kill(DWORD pid);
/* terminate all children with matching tag prefix; returns count killed */
int child_proc_kill_tag(const char *tag_prefix);
/* build text listing: "pid|cmd|tag|uptime_sec\n" ... returns malloc'd string (caller frees) */
char *child_proc_list(void);
/* prune dead entries; call periodically */
void child_proc_prune(void);

#endif
