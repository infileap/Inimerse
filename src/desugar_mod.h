#ifndef DESUGAR_MOD_H
#define DESUGAR_MOD_H

#include "vm.h"

/* CLI: inimerse --desugar in.im out.im (out optional: stdout) */
int desugar_file(const char *in, const char *out);
void desugar_mod_register(VM *vm);

#endif
