#include "thread.h"
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
static unsigned __stdcall worker(void *arg) { (void)arg; Sleep(100); return 0; }
#else
#include <unistd.h>
static void *worker(void *arg) { (void)arg; usleep(100000); return NULL; }
#endif
int main(void) {
    void *h = im_thread_start(worker, NULL);
    if (!h) return 2;
    if (im_thread_join(h, 1) == 0) return 3;
    if (im_thread_join(h, 1000) != 0) return 4;
    puts("thread probe: ok");
    return 0;
}
