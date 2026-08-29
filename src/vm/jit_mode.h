#ifndef INIMERSE_JIT_MODE_H
#define INIMERSE_JIT_MODE_H

enum ImJitMode { IM_JIT_OFF = 0, IM_JIT_TEMPLATE = 1, IM_JIT_OPTIMIZED = 2 };
extern int im_jit_mode;
int im_jit_mode_parse(const char *name);
const char *im_jit_mode_name(int mode);

#endif
