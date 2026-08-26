#include "mod.h"

/* POSIX module loader baseline. Native Windows DLL loading is disabled until
 * the PAL dynamic-library backend lands; script modules remain loadable via
 * the dedicated platform implementation. */
void mod_load_all(VM *vm, const char *mod_dir) { (void)vm; (void)mod_dir; }
void mod_load_by_name(VM *vm, const char *mod_dir, const char *name) { (void)vm; (void)mod_dir; (void)name; }

