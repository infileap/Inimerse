#ifndef INIMERSE_ENUM_H
#define INIMERSE_ENUM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Runtime descriptor for any finite collection of symbolic members. */
typedef struct ImEnum ImEnum;
typedef struct ImTypeSet ImTypeSet;

typedef enum {
    IM_ENUM_U8 = 1,
    IM_ENUM_U16 = 2,
    IM_ENUM_BOXED = 0
} ImEnumWidth;

ImEnum *im_enum_create(const char *type_name, const char *const *members, size_t count);
ImEnum *im_enum_from_typeset(const char *type_name, const ImTypeSet *set);
ImEnum *im_enum_from_finite_set(const char *type_name, const ImTypeSet *set);
void im_enum_free(ImEnum *enumeration);
const char *im_enum_type_name(const ImEnum *enumeration);
size_t im_enum_count(const ImEnum *enumeration);
ImEnumWidth im_enum_width(const ImEnum *enumeration);
size_t im_enum_width_bytes(const ImEnum *enumeration);
uint32_t im_enum_code_limit(const ImEnum *enumeration);
bool im_enum_encode(const ImEnum *enumeration, const char *name, uint32_t *code_out);
const char *im_enum_decode(const ImEnum *enumeration, uint32_t code);
int im_enum_qualified_member(const ImEnum *enumeration, uint32_t code, char *out, size_t capacity);
bool im_enum_parse_qualified(const ImEnum *enumeration, const char *qualified, uint32_t *code_out);
bool im_enum_contains(const ImEnum *enumeration, const char *name);
uint64_t im_enum_fingerprint(const ImEnum *enumeration);
bool im_enum_compatible_append(const ImEnum *old_enum, const ImEnum *new_enum);
bool im_enum_is_exhaustive(const ImEnum *enumeration, const char *const *covered, size_t covered_count);
size_t im_enum_missing(const ImEnum *enumeration, const char *const *covered, size_t covered_count,
                       const char **missing_out, size_t missing_capacity);

#endif
