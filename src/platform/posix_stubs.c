#include "../vm/vm.h"
#include <stdio.h>

static void unsupported(const char *name) { fprintf(stderr, "inimerse: capability '%s' is not available on this POSIX build yet\n", name); }
#define STUB_REG(name) void name(VM *vm) { (void)vm; }
STUB_REG(gui_mod_register) STUB_REG(build_mod_register) STUB_REG(io_mod_register)
STUB_REG(json_mod_register) STUB_REG(infiverse_mod_register)
STUB_REG(verse_dist_mod_register) STUB_REG(server_mod_register) STUB_REG(identity_mod_register)
STUB_REG(social_mod_register) STUB_REG(ai_mod_register) STUB_REG(record_mod_register)
STUB_REG(lint_mod_register)
int build_project_impl(void *vm, const char *cfg, int mode, const char *out) { (void)vm;(void)cfg;(void)mode;(void)out; unsupported("build"); return -1; }
int lint_check(const char *path, char *out, int cap) { (void)path; if (out && cap > 0) snprintf(out, (size_t)cap, "lint is unavailable on this POSIX build"); return -1; }
int headless_init(int port) { (void)port; unsupported("headless"); return 0; }
void headless_start_thread(void) { }
int verse_http_start(int port) { (void)port; unsupported("verse_http"); return 0; }
int desugar_file(const char *in, const char *out) { (void)in; (void)out; unsupported("desugar"); return -1; }
