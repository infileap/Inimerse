#include "jit_mode.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    if (im_jit_mode_parse("off") != IM_JIT_OFF) return 1;
    if (im_jit_mode_parse("template") != IM_JIT_TEMPLATE) return 2;
    if (im_jit_mode_parse("optimized") != IM_JIT_OPTIMIZED) return 3;
    if (im_jit_mode_parse("invalid") != -1) return 4;
    if (strcmp(im_jit_mode_name(IM_JIT_OFF), "off") || strcmp(im_jit_mode_name(IM_JIT_TEMPLATE), "template") || strcmp(im_jit_mode_name(IM_JIT_OPTIMIZED), "optimized")) return 5;
    puts("jit mode probe"); return 0;
}
