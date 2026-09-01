#ifndef INIMERSE_TYPESET_H
#define INIMERSE_TYPESET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    IM_TYPE_NIL = 0,
    IM_TYPE_BOOL,
    IM_TYPE_INT,
    IM_TYPE_FLOAT,
    IM_TYPE_STRING
} ImTypeValueKind;

typedef struct {
    ImTypeValueKind kind;
    int boolean;
    int64_t integer;
    double real;
    const char *string;
} ImTypeValue;

typedef enum {
    IM_TYPESET_EMPTY = 0,
    IM_TYPESET_ANY,
    IM_TYPESET_ENUM,
    IM_TYPESET_INT_INTERVAL,
    IM_TYPESET_UNION,
    IM_TYPESET_INTERSECTION,
    IM_TYPESET_DIFFERENCE,
    IM_TYPESET_COMPLEMENT
} ImTypeSetKind;

typedef struct ImTypeSet ImTypeSet;

ImTypeSet *im_typeset_empty(void);
ImTypeSet *im_typeset_any(void);
ImTypeSet *im_typeset_enum(ImTypeValueKind kind, const ImTypeValue *values, size_t count);
ImTypeSet *im_typeset_int_interval(int64_t lo, int64_t hi, bool lo_inclusive, bool hi_inclusive);
ImTypeSet *im_typeset_union(const ImTypeSet *left, const ImTypeSet *right);
ImTypeSet *im_typeset_intersection(const ImTypeSet *left, const ImTypeSet *right);
ImTypeSet *im_typeset_difference(const ImTypeSet *left, const ImTypeSet *right);
ImTypeSet *im_typeset_complement(const ImTypeSet *set);
void im_typeset_free(ImTypeSet *set);

bool im_typeset_contains(const ImTypeSet *set, const ImTypeValue *value);
bool im_typeset_subset(const ImTypeSet *left, const ImTypeSet *right);
bool im_typeset_intersects(const ImTypeSet *left, const ImTypeSet *right);
ImTypeSetKind im_typeset_kind(const ImTypeSet *set);
size_t im_typeset_enum_count(const ImTypeSet *set);
const ImTypeValue *im_typeset_enum_value(const ImTypeSet *set, size_t index);
/* Materialize a finite set expression as an enum; returns NULL for non-finite sets. */
ImTypeSet *im_typeset_materialize_enum(const ImTypeSet *set);

#endif
