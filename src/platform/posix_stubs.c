#include "../vm/vm.h"
#include <stdio.h>

static void unsupported(const char *name) { fprintf(stderr, "inimerse: capability '%s' is not available on this POSIX build yet\n", name); }
#define STUB_REG(name) void name(VM *vm) { (void)vm; }
STUB_REG(gui_mod_register) STUB_REG(build_mod_register) STUB_REG(io_mod_register)
STUB_REG(json_mod_register) STUB_REG(infiverse_mod_register)
STUB_REG(verse_dist_mod_register) STUB_REG(identity_mod_register)
STUB_REG(social_mod_register) STUB_REG(ai_mod_register) STUB_REG(record_mod_register)
int build_project_impl(void *vm, const char *cfg, int mode, const char *out) { (void)vm;(void)cfg;(void)mode;(void)out; unsupported("build"); return -1; }
/* lint_check and desugar_file are provided by their portable implementations. */
