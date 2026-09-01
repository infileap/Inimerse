#include "enum.h"
#include "typeset.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct ImEnum {
    char *type_name;
    char **members;
    size_t count;
    ImEnumWidth width;
};

static char *dupstr(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

ImEnum *im_enum_create(const char *type_name, const char *const *members, size_t count) {
    if (!type_name || (!members && count) || count > UINT32_MAX) return NULL;
    ImEnum *e = (ImEnum *)calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->type_name = dupstr(type_name);
    e->count = count;
    e->width = count <= 256 ? IM_ENUM_U8 : (count <= 65536 ? IM_ENUM_U16 : IM_ENUM_BOXED);
    if (!e->type_name) { free(e); return NULL; }
    if (count) {
        e->members = (char **)calloc(count, sizeof(char *));
        if (!e->members) { free(e->type_name); free(e); return NULL; }
        for (size_t i = 0; i < count; ++i) {
            if (!members[i] || !*members[i]) { im_enum_free(e); return NULL; }
            e->members[i] = dupstr(members[i]);
            if (!e->members[i]) { im_enum_free(e); return NULL; }
            for (size_t j = 0; j < i; ++j)
                if (strcmp(e->members[j], e->members[i]) == 0) { im_enum_free(e); return NULL; }
        }
    }
    return e;
}

ImEnum *im_enum_from_typeset(const char *type_name, const ImTypeSet *set) {
    if (!set || im_typeset_kind(set) != IM_TYPESET_ENUM) return NULL;
    size_t n = im_typeset_enum_count(set);
    const char **names = n ? (const char **)calloc(n, sizeof(*names)) : NULL;
    if (n && !names) return NULL;
    for (size_t i = 0; i < n; ++i) {
        const ImTypeValue *v = im_typeset_enum_value(set, i);
        if (!v) { free(names); return NULL; }
        if (v->kind != IM_TYPE_STRING) { free(names); return NULL; }
        names[i] = v->string;
    }
    ImEnum *e = im_enum_create(type_name, names, n);
    free(names);
    return e;
}

ImEnum *im_enum_from_finite_set(const char *type_name, const ImTypeSet *set) {
    ImTypeSet *materialized = im_typeset_materialize_enum(set);
    if (!materialized) return NULL;
    size_t n = im_typeset_enum_count(materialized);
    const char **names = n ? (const char **)calloc(n, sizeof(*names)) : NULL;
    char (*owned)[64] = n ? (char (*)[64])calloc(n, sizeof(*owned)) : NULL;
    if ((n && !names) || (n && !owned)) { free(names); free(owned); im_typeset_free(materialized); return NULL; }
    for (size_t i = 0; i < n; ++i) {
        const ImTypeValue *v = im_typeset_enum_value(materialized, i);
        if (!v) { free(names); free(owned); im_typeset_free(materialized); return NULL; }
        switch (v->kind) {
            case IM_TYPE_STRING: names[i] = v->string; break;
            case IM_TYPE_INT: snprintf(owned[i], sizeof(owned[i]), "int:%lld", (long long)v->integer); names[i] = owned[i]; break;
            case IM_TYPE_BOOL: snprintf(owned[i], sizeof(owned[i]), "bool:%s", v->boolean ? "true" : "false"); names[i] = owned[i]; break;
            case IM_TYPE_NIL: snprintf(owned[i], sizeof(owned[i]), "nil:nil"); names[i] = owned[i]; break;
            case IM_TYPE_FLOAT: snprintf(owned[i], sizeof(owned[i]), "float:%.17g", v->real); names[i] = owned[i]; break;
            default: free(names); free(owned); im_typeset_free(materialized); return NULL;
        }
    }
    ImEnum *e = im_enum_create(type_name, names, n);
    free(names); free(owned);
    im_typeset_free(materialized);
    return e;
}

void im_enum_free(ImEnum *e) {
    if (!e) return;
    for (size_t i = 0; i < e->count; ++i) free(e->members[i]);
    free(e->members); free(e->type_name); free(e);
}

const char *im_enum_type_name(const ImEnum *e) { return e ? e->type_name : NULL; }
size_t im_enum_count(const ImEnum *e) { return e ? e->count : 0; }
ImEnumWidth im_enum_width(const ImEnum *e) { return e ? e->width : IM_ENUM_BOXED; }

bool im_enum_encode(const ImEnum *e, const char *name, uint32_t *out) {
    if (!e || !name || !out) return false;
    for (size_t i = 0; i < e->count; ++i) if (strcmp(e->members[i], name) == 0) { *out = (uint32_t)i; return true; }
    return false;
}

const char *im_enum_decode(const ImEnum *e, uint32_t code) { return e && code < e->count ? e->members[code] : NULL; }
bool im_enum_contains(const ImEnum *e, const char *name) { uint32_t ignored; return im_enum_encode(e, name, &ignored); }

size_t im_enum_missing(const ImEnum *e, const char *const *covered, size_t covered_count,
                       const char **missing, size_t capacity) {
    if (!e) return 0;
    size_t n = 0;
    for (size_t i = 0; i < e->count; ++i) {
        bool found = false;
        for (size_t j = 0; j < covered_count; ++j)
            if (covered && covered[j] && strcmp(e->members[i], covered[j]) == 0) { found = true; break; }
        if (!found) { if (missing && n < capacity) missing[n] = e->members[i]; ++n; }
    }
    return n;
}

bool im_enum_is_exhaustive(const ImEnum *e, const char *const *covered, size_t covered_count) {
    return im_enum_missing(e, covered, covered_count, NULL, 0) == 0;
}
