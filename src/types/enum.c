#include "enum.h"
#include <stdlib.h>
#include <string.h>

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
