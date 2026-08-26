#include "fiber.h"
#include <stdio.h>

static ImFiber *scheduler;
static volatile int state;

static void worker(void *arg) {
    (void)arg;
    state = 1;
    im_fiber_switch(scheduler);
    state = 2;
}

int main(void) {
    scheduler = im_fiber_convert_current();
    if (!scheduler) return 2;
    ImFiber *child = im_fiber_create(64 * 1024, worker, NULL);
    if (!child) return 3;
    im_fiber_switch(child);
    if (state != 1) return 4;
    im_fiber_switch(child);
    if (state != 2) return 5;
    im_fiber_destroy(child);
    /* The current fiber is owned by the host thread and must not be freed. */
    puts("fiber probe: ok");
    return 0;
}
