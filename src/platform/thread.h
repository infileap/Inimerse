#ifndef INIMERSE_PLATFORM_THREAD_H
#define INIMERSE_PLATFORM_THREAD_H

#ifdef _WIN32
#include <windows.h>
typedef unsigned (__stdcall *ImThreadProc)(void *arg);
#else
typedef void *(*ImThreadProc)(void *arg);
#endif

/* Starts a detached-capable host thread and returns an opaque handle. The VM
 * owns the handle and is responsible for joining/closing it. */
void *im_thread_start(ImThreadProc proc, void *arg);
int im_thread_join(void *handle, unsigned int timeout_ms);
/* Detaches a thread and releases its opaque handle without waiting. */
int im_thread_detach(void *handle);
void im_thread_close(void *handle);

#endif
