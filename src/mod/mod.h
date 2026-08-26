#ifndef MOD_H
#define MOD_H
#include "vm.h"
void mod_load_all(VM *vm, const char *mod_dir);
/* 按名称加载单个模组（mod_dir/mods/<name>/mod.st） */
void mod_load_by_name(VM *vm, const char *mod_dir, const char *name);
#endif
