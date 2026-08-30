#include "registry.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    ImTypeSet *set;
} TypeEntry;

struct ImTypeRegistry {
    TypeEntry *entries;
    size_t count;
    size_t capacity;
};

ImTypeRegistry *im_type_registry_new(void) {
    return (ImTypeRegistry *)calloc(1, sizeof(ImTypeRegistry));
}

void im_type_registry_free(ImTypeRegistry *registry) {
    if (!registry) return;
    for (size_t i = 0; i < registry->count; ++i) {
        free(registry->entries[i].name);
        im_typeset_free(registry->entries[i].set);
    }
    free(registry->entries);
    free(registry);
}

int im_type_registry_define(ImTypeRegistry *registry, const char *name, ImTypeSet *set) {
    if (!registry || !name || !*name || !set) return 0;
    for (size_t i = 0; i < registry->count; ++i) {
        if (strcmp(registry->entries[i].name, name) != 0) continue;
        im_typeset_free(registry->entries[i].set);
        registry->entries[i].set = set;
        return 1;
    }
    if (registry->count == registry->capacity) {
        size_t next = registry->capacity ? registry->capacity * 2 : 8;
        TypeEntry *grown = (TypeEntry *)realloc(registry->entries, next * sizeof(*grown));
        if (!grown) return 0;
        registry->entries = grown;
        registry->capacity = next;
    }
    registry->entries[registry->count].name = strdup(name);
    if (!registry->entries[registry->count].name) return 0;
    registry->entries[registry->count].set = set;
    registry->count++;
    return 1;
}

const ImTypeSet *im_type_registry_lookup(const ImTypeRegistry *registry, const char *name) {
    if (!registry || !name) return NULL;
    for (size_t i = 0; i < registry->count; ++i)
        if (strcmp(registry->entries[i].name, name) == 0) return registry->entries[i].set;
    return NULL;
}

size_t im_type_registry_count(const ImTypeRegistry *registry) {
    return registry ? registry->count : 0;
}
