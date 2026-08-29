#include "jit_mode.h"
#include <string.h>

int im_jit_mode = IM_JIT_OFF;

int im_jit_mode_parse(const char *name) {
    if (!name || !strcmp(name, "off")) return IM_JIT_OFF;
    if (!strcmp(name, "template")) return IM_JIT_TEMPLATE;
    if (!strcmp(name, "optimized")) return IM_JIT_OPTIMIZED;
    return -1;
}

const char *im_jit_mode_name(int mode) {
    switch (mode) { case IM_JIT_TEMPLATE: return "template"; case IM_JIT_OPTIMIZED: return "optimized"; default: return "off"; }
}
