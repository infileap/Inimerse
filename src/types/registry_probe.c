#include "registry.h"

#include <assert.h>

int main(void) {
    ImTypeRegistry *registry = im_type_registry_new();
    ImTypeValue one = {.kind = IM_TYPE_INT, .integer = 1};
    ImTypeSet *positive = im_typeset_int_interval(0, 100, false, true);
    assert(registry && positive);
    assert(im_type_registry_define(registry, "Positive", positive));
    assert(im_type_registry_count(registry) == 1);
    assert(im_typeset_contains(im_type_registry_lookup(registry, "Positive"), &one));
    assert(im_type_registry_lookup(registry, "Missing") == NULL);
    ImTypeValue modes[] = {{.kind = IM_TYPE_STRING, .string = "easy"}, {.kind = IM_TYPE_STRING, .string = "hard"}};
    assert(im_type_registry_define(registry, "Mode", im_typeset_enum(IM_TYPE_STRING, modes, 2)));
    ImEnum *mode_enum = im_type_registry_enum(registry, "Mode");
    assert(mode_enum && im_enum_contains(mode_enum, "hard"));
    im_enum_free(mode_enum);
    im_type_registry_free(registry);
    return 0;
}
