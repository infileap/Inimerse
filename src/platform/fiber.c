#ifndef _WIN32
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#endif
#include "fiber.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
struct ImFiber { void *handle; };
typedef struct { ImFiberProc proc; void *arg; } ImFiberStart;
static VOID WINAPI im_fiber_trampoline(LPVOID raw) {
    ImFiberStart *start = (ImFiberStart *)raw;
    start->proc(start->arg); free(start);
}
ImFiber *im_fiber_convert_current(void) {
    void *handle = ConvertThreadToFiber(NULL);
    if (!handle) return NULL;
    ImFiber *fiber = (ImFiber *)calloc(1, sizeof(*fiber)); fiber->handle = handle; return fiber;
}
ImFiber *im_fiber_create(size_t stack_size, ImFiberProc proc, void *arg) {
    ImFiberStart *start = (ImFiberStart *)malloc(sizeof(*start));
    if (!start) return NULL; start->proc = proc; start->arg = arg;
    ImFiber *fiber = (ImFiber *)calloc(1, sizeof(*fiber));
    if (!fiber) { free(start); return NULL; }
    fiber->handle = CreateFiber(stack_size, im_fiber_trampoline, start);
    if (!fiber->handle) { free(start); free(fiber); return NULL; }
    return fiber;
}
void im_fiber_switch(ImFiber *target) { if (target) SwitchToFiber(target->handle); }
void im_fiber_destroy(ImFiber *fiber) { if (!fiber) return; if (fiber->handle) DeleteFiber(fiber->handle); free(fiber); }
#else
#include <ucontext.h>
#include <stdint.h>
#include <stdlib.h>
struct ImFiber { ucontext_t ctx; void *stack; size_t stack_size; ImFiberProc proc; void *arg; };
static _Thread_local ImFiber *current_fiber;
static void im_fiber_trampoline(uintptr_t raw) {
    ImFiber *fiber = (ImFiber *)raw;
    current_fiber = fiber; fiber->proc(fiber->arg);
}
ImFiber *im_fiber_convert_current(void) {
    ImFiber *fiber = (ImFiber *)calloc(1, sizeof(*fiber));
    if (!fiber || getcontext(&fiber->ctx) != 0) { free(fiber); return NULL; }
    current_fiber = fiber; return fiber;
}
ImFiber *im_fiber_create(size_t stack_size, ImFiberProc proc, void *arg) {
    ImFiber *fiber = (ImFiber *)calloc(1, sizeof(*fiber));
    if (!fiber) return NULL;
    fiber->stack_size = stack_size ? stack_size : 256 * 1024; fiber->proc = proc; fiber->arg = arg;
    fiber->stack = malloc(fiber->stack_size);
    if (!fiber->stack || getcontext(&fiber->ctx) != 0) { free(fiber->stack); free(fiber); return NULL; }
    fiber->ctx.uc_stack.ss_sp = fiber->stack; fiber->ctx.uc_stack.ss_size = fiber->stack_size;
    fiber->ctx.uc_link = NULL; makecontext(&fiber->ctx, (void (*)(void))im_fiber_trampoline, 1, (uintptr_t)fiber);
    return fiber;
}
void im_fiber_switch(ImFiber *target) {
    if (!target || !current_fiber) return;
    ImFiber *from = current_fiber; current_fiber = target;
    if (swapcontext(&from->ctx, &target->ctx) != 0) current_fiber = from;
}
void im_fiber_destroy(ImFiber *fiber) { if (!fiber) return; if (current_fiber == fiber) current_fiber = NULL; free(fiber->stack); free(fiber); }
#endif
