#include "bytecode.h"
#include <assert.h>
#include <string.h>

int main(void) {
    Bytecode bc;
    bytecode_init(&bc);
    assert(bytecode_add_capture(&bc, "base") == 0);
    assert(bytecode_add_capture(&bc, "offset") == 1);
    assert(bytecode_add_capture(&bc, "base") == 0);
    assert(bc.capture_count == 2);
    assert(strcmp(bc.capture_names[0], "base") == 0);
    assert(strcmp(bc.capture_names[1], "offset") == 0);
    assert(bytecode_add_capture(&bc, NULL) == -1);
    bytecode_free(&bc);
    return 0;
}
