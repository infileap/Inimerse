#ifndef INIMERSE_PLATFORM_FIBER_H
#define INIMERSE_PLATFORM_FIBER_H

#include <stddef.h>

typedef void (*ImFiberProc)(void *arg);
typedef struct ImFiber ImFiber;

ImFiber *im_fiber_convert_current(void);
ImFiber *im_fiber_create(size_t stack_size, ImFiberProc proc, void *arg);
void im_fiber_switch(ImFiber *target);
void im_fiber_destroy(ImFiber *fiber);

#endif
