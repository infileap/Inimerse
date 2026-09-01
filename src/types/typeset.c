#include "typeset.h"

#include <stdlib.h>
#include <string.h>

struct ImTypeSet {
    ImTypeSetKind kind;
    ImTypeValueKind value_kind;
    ImTypeValue *values;
    size_t value_count;
    int64_t lo, hi;
    bool lo_inclusive, hi_inclusive;
    struct ImTypeSet *left;
    struct ImTypeSet *right;
};

static ImTypeSet *alloc_set(ImTypeSetKind kind) {
    ImTypeSet *set = (ImTypeSet *)calloc(1, sizeof(*set));
    if (set) set->kind = kind;
    return set;
}

ImTypeSet *im_typeset_empty(void) { return alloc_set(IM_TYPESET_EMPTY); }
ImTypeSet *im_typeset_any(void) { return alloc_set(IM_TYPESET_ANY); }

ImTypeSet *im_typeset_enum(ImTypeValueKind kind, const ImTypeValue *values, size_t count) {
    ImTypeSet *set = alloc_set(IM_TYPESET_ENUM);
    if (!set) return NULL;
    set->value_kind = kind;
    if (count != 0) {
        set->values = (ImTypeValue *)malloc(count * sizeof(*set->values));
        if (!set->values) { free(set); return NULL; }
        memcpy(set->values, values, count * sizeof(*set->values));
        set->value_count = count;
    }
    return set;
}

ImTypeSet *im_typeset_int_interval(int64_t lo, int64_t hi, bool lo_inclusive, bool hi_inclusive) {
    ImTypeSet *set = alloc_set(IM_TYPESET_INT_INTERVAL);
    if (!set) return NULL;
    set->value_kind = IM_TYPE_INT;
    set->lo = lo; set->hi = hi;
    set->lo_inclusive = lo_inclusive; set->hi_inclusive = hi_inclusive;
    return set;
}

static ImTypeSet *combine(ImTypeSetKind kind, const ImTypeSet *left, const ImTypeSet *right) {
    ImTypeSet *set = alloc_set(kind);
    if (!set) return NULL;
    set->left = (ImTypeSet *)left;
    set->right = (ImTypeSet *)right;
    return set;
}

ImTypeSet *im_typeset_union(const ImTypeSet *left, const ImTypeSet *right) {
    if (!left || !right) return NULL;
    if (left->kind == IM_TYPESET_ANY || right->kind == IM_TYPESET_ANY) return im_typeset_any();
    if (left->kind == IM_TYPESET_EMPTY) return (ImTypeSet *)right;
    if (right->kind == IM_TYPESET_EMPTY) return (ImTypeSet *)left;
    return combine(IM_TYPESET_UNION, left, right);
}

ImTypeSet *im_typeset_intersection(const ImTypeSet *left, const ImTypeSet *right) {
    if (!left || !right) return NULL;
    if (left->kind == IM_TYPESET_EMPTY || right->kind == IM_TYPESET_EMPTY) return im_typeset_empty();
    if (left->kind == IM_TYPESET_ANY) return (ImTypeSet *)right;
    if (right->kind == IM_TYPESET_ANY) return (ImTypeSet *)left;
    return combine(IM_TYPESET_INTERSECTION, left, right);
}

ImTypeSet *im_typeset_difference(const ImTypeSet *left, const ImTypeSet *right) {
    if (!left || !right) return NULL;
    if (left->kind == IM_TYPESET_EMPTY || right->kind == IM_TYPESET_ANY) return im_typeset_empty();
    if (right->kind == IM_TYPESET_EMPTY) return (ImTypeSet *)left;
    return combine(IM_TYPESET_DIFFERENCE, left, right);
}

ImTypeSet *im_typeset_complement(const ImTypeSet *set) {
    if (!set) return NULL;
    if (set->kind == IM_TYPESET_ANY) return im_typeset_empty();
    if (set->kind == IM_TYPESET_EMPTY) return im_typeset_any();
    return combine(IM_TYPESET_COMPLEMENT, set, NULL);
}

void im_typeset_free(ImTypeSet *set) {
    if (!set) return;
    /* Union/intersection operands are borrowed; callers retain their ownership. */
    free(set->values);
    free(set);
}

static bool value_equal(const ImTypeValue *a, const ImTypeValue *b) {
    if (!a || !b || a->kind != b->kind) return false;
    switch (a->kind) {
    case IM_TYPE_NIL: return true;
    case IM_TYPE_BOOL: return a->boolean == b->boolean;
    case IM_TYPE_INT: return a->integer == b->integer;
    case IM_TYPE_FLOAT: return a->real == b->real;
    case IM_TYPE_STRING: return a->string && b->string && strcmp(a->string, b->string) == 0;
    }
    return false;
}

bool im_typeset_contains(const ImTypeSet *set, const ImTypeValue *value) {
    if (!set || !value) return false;
    switch (set->kind) {
    case IM_TYPESET_EMPTY: return false;
    case IM_TYPESET_ANY: return true;
    case IM_TYPESET_ENUM:
        for (size_t i = 0; i < set->value_count; ++i)
            if (value_equal(&set->values[i], value)) return true;
        return false;
    case IM_TYPESET_INT_INTERVAL: {
        if (value->kind != IM_TYPE_INT) return false;
        bool lower = set->lo_inclusive ? value->integer >= set->lo : value->integer > set->lo;
        bool upper = set->hi_inclusive ? value->integer <= set->hi : value->integer < set->hi;
        return lower && upper;
    }
    case IM_TYPESET_UNION:
        return im_typeset_contains(set->left, value) || im_typeset_contains(set->right, value);
    case IM_TYPESET_INTERSECTION:
        return im_typeset_contains(set->left, value) && im_typeset_contains(set->right, value);
    case IM_TYPESET_DIFFERENCE:
        return im_typeset_contains(set->left, value) && !im_typeset_contains(set->right, value);
    case IM_TYPESET_COMPLEMENT:
        return !im_typeset_contains(set->left, value);
    }
    return false;
}

bool im_typeset_subset(const ImTypeSet *left, const ImTypeSet *right) {
    if (!left || !right) return false;
    if (left->kind == IM_TYPESET_EMPTY || right->kind == IM_TYPESET_ANY) return true;
    if (right->kind == IM_TYPESET_EMPTY) return left->kind == IM_TYPESET_EMPTY;
    if (left->kind == IM_TYPESET_ENUM) {
        for (size_t i = 0; i < left->value_count; ++i)
            if (!im_typeset_contains(right, &left->values[i])) return false;
        return true;
    }
    if (left->kind == IM_TYPESET_UNION)
        return im_typeset_subset(left->left, right) && im_typeset_subset(left->right, right);
    if (left->kind == IM_TYPESET_INTERSECTION)
        return im_typeset_subset(left->left, right) || im_typeset_subset(left->right, right);
    return false;
}

bool im_typeset_intersects(const ImTypeSet *left, const ImTypeSet *right) {
    if (!left || !right || left->kind == IM_TYPESET_EMPTY || right->kind == IM_TYPESET_EMPTY) return false;
    if (left->kind == IM_TYPESET_ANY || right->kind == IM_TYPESET_ANY) return true;
    if (left->kind == IM_TYPESET_ENUM) {
        for (size_t i = 0; i < left->value_count; ++i)
            if (im_typeset_contains(right, &left->values[i])) return true;
        return false;
    }
    if (right->kind == IM_TYPESET_ENUM) return im_typeset_intersects(right, left);
    if (left->kind == IM_TYPESET_UNION || left->kind == IM_TYPESET_DIFFERENCE)
        return im_typeset_intersects(left->left, right) ||
               (left->kind == IM_TYPESET_UNION && im_typeset_intersects(left->right, right));
    if (right->kind == IM_TYPESET_UNION || right->kind == IM_TYPESET_DIFFERENCE)
        return im_typeset_intersects(right, left);
    if (left->kind == IM_TYPESET_INTERSECTION)
        return im_typeset_intersects(left->left, right) && im_typeset_intersects(left->right, right);
    if (right->kind == IM_TYPESET_INTERSECTION)
        return im_typeset_intersects(right, left);
    if (left->kind == IM_TYPESET_COMPLEMENT) return !im_typeset_subset(right, left->left);
    if (right->kind == IM_TYPESET_COMPLEMENT) return !im_typeset_subset(left, right->left);
    if (left->kind == IM_TYPESET_INT_INTERVAL && right->kind == IM_TYPESET_INT_INTERVAL) {
        if (left->hi < right->lo || right->hi < left->lo) return false;
        return true;
    }
    return false;
}

ImTypeSetKind im_typeset_kind(const ImTypeSet *set) {
    return set ? set->kind : IM_TYPESET_EMPTY;
}

/* Forward declaration: used by exact-cardinality queries on finite expressions. */
ImTypeSet *im_typeset_materialize_enum(const ImTypeSet *set);

size_t im_typeset_cardinality(const ImTypeSet *set) {
    if (!set) return 0;
    switch (set->kind) {
        case IM_TYPESET_EMPTY: return 0;
        case IM_TYPESET_ENUM: return set->value_count;
        case IM_TYPESET_ANY: return SIZE_MAX;
        case IM_TYPESET_INT_INTERVAL:
            if (set->lo_inclusive && set->hi_inclusive && set->hi >= set->lo) {
                uint64_t span = (uint64_t)set->hi - (uint64_t)set->lo;
                return span < SIZE_MAX ? (size_t)(span + 1) : SIZE_MAX;
            }
            return SIZE_MAX;
        case IM_TYPESET_UNION: {
            ImTypeSet *m = im_typeset_materialize_enum(set);
            if (!m) return SIZE_MAX;
            size_t n = m->value_count;
            im_typeset_free(m);
            return n;
        }
        case IM_TYPESET_INTERSECTION:
        case IM_TYPESET_DIFFERENCE: {
            ImTypeSet *m = im_typeset_materialize_enum(set);
            if (!m) return SIZE_MAX;
            size_t n = m->value_count;
            im_typeset_free(m);
            return n;
        }
        case IM_TYPESET_COMPLEMENT: return SIZE_MAX;
    }
    return SIZE_MAX;
}

size_t im_typeset_enum_count(const ImTypeSet *set) {
    return set && set->kind == IM_TYPESET_ENUM ? set->value_count : 0;
}

const ImTypeValue *im_typeset_enum_value(const ImTypeSet *set, size_t index) {
    return set && set->kind == IM_TYPESET_ENUM && index < set->value_count ? &set->values[index] : NULL;
}

ImTypeSet *im_typeset_materialize_enum(const ImTypeSet *set) {
    if (!set) return NULL;
    if (set->kind == IM_TYPESET_ENUM) return im_typeset_enum(set->value_kind, set->values, set->value_count);
    if (set->kind != IM_TYPESET_UNION && set->kind != IM_TYPESET_INTERSECTION && set->kind != IM_TYPESET_DIFFERENCE) return NULL;
    ImTypeSet *left = im_typeset_materialize_enum(set->left);
    ImTypeSet *right = im_typeset_materialize_enum(set->right);
    if (!left || !right) { im_typeset_free(left); im_typeset_free(right); return NULL; }
    size_t cap = left->value_count + right->value_count;
    ImTypeValue *vals = cap ? (ImTypeValue *)calloc(cap, sizeof(*vals)) : NULL;
    if (cap && !vals) { im_typeset_free(left); im_typeset_free(right); return NULL; }
    size_t n = 0;
    for (size_t i = 0; i < left->value_count; ++i) {
        bool keep = set->kind != IM_TYPESET_INTERSECTION || im_typeset_contains(right, &left->values[i]);
        if (set->kind == IM_TYPESET_DIFFERENCE) keep = !im_typeset_contains(right, &left->values[i]);
        if (keep) vals[n++] = left->values[i];
    }
    if (set->kind == IM_TYPESET_UNION) {
        for (size_t i = 0; i < right->value_count; ++i) {
            if (!im_typeset_contains(left, &right->values[i])) vals[n++] = right->values[i];
        }
    }
    /* A union may contain heterogeneous scalar values; retain each value's kind. */
    ImTypeSet *out = im_typeset_enum(left->value_kind, vals, n);
    free(vals); im_typeset_free(left); im_typeset_free(right);
    return out;
}
