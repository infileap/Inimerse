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
    puts("process probe: ok");
    return 0;
}
