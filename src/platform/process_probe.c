/* Windows ran this red intermittently and the log could not say why: the only
 * line printed was the pid, so the four distinct return codes were invisible.
 * Every failure now names its step and prints the value it saw.
 *
 * The old step 6 waited just 20 ms for a child to be observably still running,
 * which cmd.exe startup alone can lose -- that is a race, not a verdict.  The
 * wait is now 500 ms against a child that runs for seconds, and the precondition
 * (the slow child is alive before the short wait) is checked instead of assumed,
 * so "the environment could not start the slow command" is reported as itself
 * rather than as a timeout failure. */
#include "im_process.h"
#include <stdio.h>

int main(void) {
#ifdef _WIN32
    const char *cmd = "cmd /c exit 0";
    const char *slow = "cmd /c ping 127.0.0.1 -n 4 >nul";
    const char *bad = "cmd /c exit 7";
#else
    const char *cmd = "true";
    const char *slow = "sleep 2";
    const char *bad = "sh -c 'exit 7'";
#endif

    ImProcess *p = im_process_spawn(cmd, 0);
    if (!p) { printf("process_probe: FAIL spawn(%s)\n", cmd); return 1; }
    printf("process_pid=%llu\n", (unsigned long long)im_process_pid(p));
    int wk = im_process_wait_kill(p, 3000);
    if (wk != 0) { printf("process_probe: FAIL wait_kill(%s) rc=%d want 0\n", cmd, wk); return 1; }
    if (im_process_alive(p)) { printf("process_probe: FAIL still alive after wait_kill(%s)\n", cmd); return 1; }
    im_process_close(p);

    ImProcess *q = im_process_spawn(slow, 0);
    if (!q) { printf("process_probe: FAIL spawn(%s)\n", slow); return 1; }
    if (!im_process_alive(q)) {
        printf("process_probe: FAIL %s had already finished; cannot time a wait against it\n", slow);
        im_process_close(q); return 1;
    }
    int timed = im_process_wait_kill(q, 500);
    if (timed != 1) { printf("process_probe: FAIL wait_kill(%s, 500) rc=%d want 1\n", slow, timed); im_process_close(q); return 1; }
    int killed_code = im_process_exit_code(q);
    if (killed_code < 0) { printf("process_probe: FAIL exit_code after kill=%d want >= 0\n", killed_code); im_process_close(q); return 1; }
    im_process_close(q);

    ImProcess *b = im_process_spawn(bad, 0);
    if (!b) { printf("process_probe: FAIL spawn(%s)\n", bad); return 1; }
    int w = im_process_wait(b, 3000);
    int bc = im_process_exit_code(b);
    if (w != 0 || bc != 7) {
        printf("process_probe: FAIL wait(%s) rc=%d exit_code=%d want rc=0 exit_code=7\n", bad, w, bc);
        im_process_close(b); return 1;
    }
    im_process_close(b);

    puts("process probe: ok");
    return 0;
}
