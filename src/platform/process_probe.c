#include "process.h"
#include <stdio.h>

int main(void) {
#ifdef _WIN32
    const char *cmd = "cmd /c exit 0";
#else
    const char *cmd = "true";
#endif
    ImProcess *p = im_process_spawn(cmd, 0);
    if (!p) return 2;
    printf("process_pid=%llu\n", (unsigned long long)im_process_pid(p));
    if (im_process_wait_kill(p, 3000) != 0) return 3;
    if (im_process_alive(p)) return 4;
    im_process_close(p);
 #ifdef _WIN32
    const char *slow = "ping 127.0.0.1 -n 4 >nul";
 #else
    const char *slow = "sleep 2";
 #endif
    ImProcess *q = im_process_spawn(slow, 0); if (!q) return 5;
    int timed = im_process_wait_kill(q, 20); if (timed != 1) return 6;
    if (im_process_exit_code(q) < 0) return 7;
    im_process_close(q);
 #ifdef _WIN32
    const char *bad = "cmd /c exit 7";
 #else
    const char *bad = "sh -c 'exit 7'";
 #endif
    ImProcess *b = im_process_spawn(bad, 0); if (!b) return 8;
    if (im_process_wait(b, 3000) != 0 || im_process_exit_code(b) != 7) return 9;
    im_process_close(b);
    puts("process probe: ok");
    return 0;
}
