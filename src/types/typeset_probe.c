#include "typeset.h"

#include <assert.h>

int main(void) {
    ImTypeValue values[] = {
        {.kind = IM_TYPE_INT, .integer = 0},
        {.kind = IM_TYPE_INT, .integer = 1},
        {.kind = IM_TYPE_INT, .integer = 2}
    };
    ImTypeValue one = {.kind = IM_TYPE_INT, .integer = 1};
    ImTypeValue four = {.kind = IM_TYPE_INT, .integer = 4};
    ImTypeValue text = {.kind = IM_TYPE_STRING, .string = "1"};

    ImTypeSet *enum_set = im_typeset_enum(IM_TYPE_INT, values, 3);
    ImTypeSet *byte = im_typeset_int_interval(0, 255, true, true);
    ImTypeSet *positive = im_typeset_int_interval(0, INT64_MAX, false, true);
    ImTypeSet *safe_positive = im_typeset_intersection(byte, positive);
    ImTypeSet *combined = im_typeset_union(enum_set, byte);
    ImTypeSet *zero_set = im_typeset_enum(IM_TYPE_INT, values, 1);
    ImTypeSet *without_zero = im_typeset_difference(byte, zero_set);
    ImTypeSet *not_enum = im_typeset_complement(enum_set);

    assert(im_typeset_contains(enum_set, &one));
    assert(!im_typeset_contains(enum_set, &four));
    assert(im_typeset_contains(byte, &four));
    assert(im_typeset_contains(safe_positive, &one));
    assert(!im_typeset_contains(safe_positive, &(ImTypeValue){.kind = IM_TYPE_INT, .integer = 0}));
    assert(!im_typeset_contains(byte, &text));
    assert(im_typeset_contains(combined, &four));
    assert(im_typeset_contains(without_zero, &four));
    assert(!im_typeset_contains(without_zero, &(ImTypeValue){.kind = IM_TYPE_INT, .integer = 0}));
    assert(!im_typeset_contains(not_enum, &one));
    assert(im_typeset_contains(not_enum, &four));
    assert(im_typeset_subset(enum_set, byte));

    im_typeset_free(combined);
    im_typeset_free(without_zero);
    im_typeset_free(zero_set);
    im_typeset_free(not_enum);
    im_typeset_free(safe_positive);
    im_typeset_free(positive);
    im_typeset_free(byte);
    im_typeset_free(enum_set);
    return 0;
}
