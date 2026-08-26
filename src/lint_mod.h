/* lint_mod.h - static linter for .im scripts */
#ifndef LINT_MOD_H
#define LINT_MOD_H
#include "vm.h"
int lint_check(const char *path, char *out, int cap);
void lint_mod_register(VM *vm);
#endif
