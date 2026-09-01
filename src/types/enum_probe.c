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
    const char *covered[] = {"idle", "stopped"};
    const char *missing[3] = {0};
    assert(!im_enum_is_exhaustive(e, covered, 2));
    assert(im_enum_missing(e, covered, 2, missing, 3) == 1 && strcmp(missing[0], "running") == 0);
    const char *all[] = {"idle", "running", "stopped", "idle"};
    assert(im_enum_is_exhaustive(e, all, 4));
    im_enum_free(e);

    /* Width selection is defined by representable member codes. */
    const size_t sizes[] = {256, 257, 65536, 65537};
    const ImEnumWidth widths[] = {IM_ENUM_U8, IM_ENUM_U16, IM_ENUM_U16, IM_ENUM_BOXED};
    for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); ++si) {
        size_t n = sizes[si];
        char **names = (char **)calloc(n, sizeof(*names));
        assert(names);
        for (size_t i = 0; i < n; ++i) {
            names[i] = (char *)malloc(32);
            assert(names[i]);
            snprintf(names[i], 32, "member_%zu", i);
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

    ImTypeValue av[] = {{.kind = IM_TYPE_STRING, .string = "a"}, {.kind = IM_TYPE_STRING, .string = "b"}};
    ImTypeValue bv[] = {{.kind = IM_TYPE_STRING, .string = "b"}, {.kind = IM_TYPE_STRING, .string = "c"}};
    ImTypeSet *sa = im_typeset_enum(IM_TYPE_STRING, av, 2);
    ImTypeSet *sb = im_typeset_enum(IM_TYPE_STRING, bv, 2);
    ImTypeSet *su = im_typeset_union(sa, sb);
    ImEnum *union_enum = im_enum_from_finite_set("Letters", su);
    assert(union_enum && im_enum_count(union_enum) == 3 && im_enum_contains(union_enum, "c"));
    im_enum_free(union_enum);
    ImTypeSet *sd = im_typeset_difference(su, sb);
    ImEnum *diff_enum = im_enum_from_finite_set("OnlyA", sd);
    assert(diff_enum && im_enum_count(diff_enum) == 1 && im_enum_contains(diff_enum, "a"));
    im_enum_free(diff_enum);
    im_typeset_free(sd); im_typeset_free(su); im_typeset_free(sa); im_typeset_free(sb);

    ImTypeValue iv[] = {{.kind = IM_TYPE_INT, .integer = -1}, {.kind = IM_TYPE_INT, .integer = 7}};
    ImTypeSet *is = im_typeset_enum(IM_TYPE_INT, iv, 2);
    ImEnum *ie = im_enum_from_finite_set("Code", is);
    assert(ie && im_enum_contains(ie, "int:-1") && im_enum_contains(ie, "int:7"));
    im_enum_free(ie); im_typeset_free(is);

    ImTypeValue fv[] = {{.kind = IM_TYPE_FLOAT, .real = 0.1}, {.kind = IM_TYPE_FLOAT, .real = 3.141592653589793}};
    ImTypeSet *fs = im_typeset_enum(IM_TYPE_FLOAT, fv, 2);
    ImEnum *fe = im_enum_from_finite_set("Constants", fs);
    assert(fe && im_enum_contains(fe, "float:0.10000000000000001") && im_enum_contains(fe, "float:3.1415926535897931"));
    im_enum_free(fe); im_typeset_free(fs);

    ImTypeValue mixed_a[] = {{.kind = IM_TYPE_INT, .integer = 1}};
    ImTypeValue mixed_b[] = {{.kind = IM_TYPE_STRING, .string = "one"}};
    ImTypeSet *ma = im_typeset_enum(IM_TYPE_INT, mixed_a, 1);
    ImTypeSet *mb = im_typeset_enum(IM_TYPE_STRING, mixed_b, 1);
    ImTypeSet *mu = im_typeset_union(ma, mb);
    ImEnum *me = im_enum_from_finite_set("Mixed", mu);
    assert(me && im_enum_count(me) == 2 && im_enum_contains(me, "int:1") && im_enum_contains(me, "one"));
    im_enum_free(me); im_typeset_free(mu); im_typeset_free(ma); im_typeset_free(mb);
    puts("enum_probe: ok");
    return 0;
}
