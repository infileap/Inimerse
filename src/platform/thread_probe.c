#include "thread.h"
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
static unsigned __stdcall worker(void *arg) { (void)arg; Sleep(100); return 0; }
#else
#include <unistd.h>
static void *worker(void *arg) { (void)arg; usleep(100000); return NULL; }
#endif
static volatile int detached_done;
#ifdef _WIN32
static unsigned __stdcall detached_worker(void *arg) { (void)arg; detached_done = 1; return 0; }
#else
static void *detached_worker(void *arg) { (void)arg; detached_done = 1; return NULL; }
#endif
int main(void) {
    void *h = im_thread_start(worker, NULL);
    if (!h) return 2;
    if (im_thread_join(h, 1) == 0) return 3;
    if (im_thread_join(h, 1000) != 0) return 4;
    void *detached = im_thread_start(detached_worker, NULL);
    if (!detached || im_thread_detach(detached) != 0) return 5;
    for (int i = 0; i < 100 && !detached_done; ++i) {
#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif
    }
    if (!detached_done) return 6;
    puts("thread probe: ok");
    return 0;
}
