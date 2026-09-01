#include "enum.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const char *m[] = {"idle", "running", "stopped"};
    ImEnum *e = im_enum_create("ThreadState", m, 3);
    assert(e && im_enum_width(e) == IM_ENUM_U8);
    uint32_t code = 99;
    assert(im_enum_encode(e, "running", &code) && code == 1);
    assert(im_enum_decode(e, code) && im_enum_contains(e, "idle"));
    assert(!im_enum_encode(e, "missing", &code));
    assert(!im_enum_decode(e, 99));
    im_enum_free(e);
    puts("enum_probe: ok");
    return 0;
}
