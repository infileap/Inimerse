#include "thread.h"
#include <stdlib.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>

typedef struct { ImThreadProc proc; void *arg; } ImThreadStart;
static unsigned __stdcall im_thread_trampoline(void *raw) {
    ImThreadStart *start = (ImThreadStart *)raw;
    ImThreadProc proc = start->proc; void *arg = start->arg;
    free(start);
    proc(arg);
    return 0;
}

void *im_thread_start(ImThreadProc proc, void *arg) {
    ImThreadStart *start = (ImThreadStart *)malloc(sizeof(*start));
    if (!start) return NULL;
    start->proc = proc; start->arg = arg;
    uintptr_t handle = _beginthreadex(NULL, 0, im_thread_trampoline, start, 0, NULL);
    if (!handle) free(start);
    return (void *)handle;
}

int im_thread_join(void *handle, unsigned int timeout_ms) {
    (void)timeout_ms;
    if (!handle) return -1;
    return WaitForSingleObject((HANDLE)handle, timeout_ms) == WAIT_OBJECT_0 ? 0 : -1;
}

void im_thread_close(void *handle) {
    if (handle) CloseHandle((HANDLE)handle);
}
#else
#include <pthread.h>
#include <stdlib.h>

typedef struct { ImThreadProc proc; void *arg; } ImThreadStart;
static void *im_thread_trampoline(void *raw) {
    ImThreadStart *start = (ImThreadStart *)raw;
    ImThreadProc proc = start->proc; void *arg = start->arg;
    free(start);
    return proc(arg);
}

void *im_thread_start(ImThreadProc proc, void *arg) {
    pthread_t *thread = (pthread_t *)malloc(sizeof(*thread));
    ImThreadStart *start = (ImThreadStart *)malloc(sizeof(*start));
    if (!thread || !start) { free(thread); free(start); return NULL; }
    start->proc = proc; start->arg = arg;
    if (pthread_create(thread, NULL, im_thread_trampoline, start) != 0) {
        free(thread); free(start); return NULL;
    }
    return thread;
}

int im_thread_join(void *handle, unsigned int timeout_ms) {
    (void)timeout_ms;
    if (!handle) return -1;
    pthread_t *thread = (pthread_t *)handle;
    int rc = pthread_join(*thread, NULL);
    free(thread);
    return rc;
}

void im_thread_close(void *handle) {
    /* A join consumes and frees the POSIX handle. */
    (void)handle;
}
#endif
