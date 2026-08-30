#include "error_types.h"

#include <assert.h>

int main(void) {
    const ImErrorKind *division = im_error_kind_lookup("division_by_zero");
    const ImErrorKind *missing = im_error_kind_lookup("not_found");
    assert(division && missing);
    assert(im_error_kind_in_domain(division, IM_ERROR_DOMAIN_ARITHMETIC_VM));
    assert(im_error_kind_in_domain(missing, IM_ERROR_DOMAIN_FILE));
    assert(im_error_kind_count() >= 10);

    ImTypeSet *file = im_error_domain_set(IM_ERROR_DOMAIN_FILE);
    ImTypeValue not_found = {.kind = IM_TYPE_STRING, .string = "not_found"};
    ImTypeValue overflow = {.kind = IM_TYPE_STRING, .string = "numeric_overflow"};
    assert(file);
    assert(im_typeset_contains(file, &not_found));
    assert(!im_typeset_contains(file, &overflow));
    im_typeset_free(file);
    return 0;
}
