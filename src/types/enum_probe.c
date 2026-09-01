#include "enum.h"
#include "typeset.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    /* Width selection is defined by representable member codes. */
    const size_t sizes[] = {256, 257, 65536, 65537};
    const ImEnumWidth widths[] = {IM_ENUM_U8, IM_ENUM_U16, IM_ENUM_U16, IM_ENUM_BOXED};
    for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); ++si) {
        size_t n = sizes[si];
        char **names = (char **)calloc(n, sizeof(*names));
        assert(names);
        for (size_t i = 0; i < n; ++i) {
            names[i] = (char *)malloc(24);
            assert(names[i]);
            snprintf(names[i], 24, "member_%zu", i);
        }
        ImEnum *large = im_enum_create("Boundary", (const char *const *)names, n);
        assert(large && im_enum_width(large) == widths[si]);
        uint32_t last = 0;
        assert(im_enum_encode(large, names[n - 1], &last) && last == n - 1);
        assert(strcmp(im_enum_decode(large, last), names[n - 1]) == 0);
        im_enum_free(large);
        for (size_t i = 0; i < n; ++i) free(names[i]);
        free(names);
    }

    const char *duplicate[] = {"same", "same"};
    assert(!im_enum_create("Duplicate", duplicate, 2));

    ImTypeValue tv[] = {{.kind = IM_TYPE_STRING, .string = "cold"},
                        {.kind = IM_TYPE_STRING, .string = "hot"}};
    ImTypeSet *set = im_typeset_enum(IM_TYPE_STRING, tv, 2);
    ImEnum *from_set = im_enum_from_typeset("Temperature", set);
    assert(from_set && im_enum_contains(from_set, "hot"));
    im_enum_free(from_set);
    im_typeset_free(set);
    puts("enum_probe: ok");
    return 0;
}
