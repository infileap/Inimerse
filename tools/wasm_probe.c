#include <stdint.h>

/* Minimal freestanding probe used to validate the wasm32-wasi compiler/linker
 * wiring without depending on the desktop runtime or GUI modules. */
__attribute__((export_name("inimerse_probe")))
int32_t inimerse_probe(void) { return 0x0300; }

__attribute__((export_name("inimerse_abi_version")))
int32_t inimerse_abi_version(void) { return 1; }

__attribute__((export_name("inimerse_capabilities")))
int32_t inimerse_capabilities(void) { return 0; }
