#include "error_types.h"

#include <assert.h>
#include <string.h>

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

    ImEnum *file_enum = im_error_domain_enum(IM_ERROR_DOMAIN_FILE);
    uint32_t code = 0;
    assert(file_enum && strcmp(im_enum_type_name(file_enum), "FileError") == 0);
    assert(im_enum_encode(file_enum, "not_found", &code));
    assert(strcmp(im_enum_decode(file_enum, code), "not_found") == 0);
    assert(!im_enum_contains(file_enum, "numeric_overflow"));
    im_enum_free(file_enum);
    return 0;
}
