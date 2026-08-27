#include <stdint.h>

/* Minimal freestanding probe used to validate the wasm32-wasi compiler/linker
 * wiring without depending on the desktop runtime or GUI modules. */
__attribute__((export_name("inimerse_probe")))
int32_t inimerse_probe(void) { return 0x0300; }
