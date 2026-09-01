#ifndef INIMERSE_TYPE_REGISTRY_H
#define INIMERSE_TYPE_REGISTRY_H

#include <stddef.h>
#include "typeset.h"
#include "enum.h"

typedef struct ImTypeRegistry ImTypeRegistry;

ImTypeRegistry *im_type_registry_new(void);
void im_type_registry_free(ImTypeRegistry *registry);
int im_type_registry_define(ImTypeRegistry *registry, const char *name, ImTypeSet *set);
const ImTypeSet *im_type_registry_lookup(const ImTypeRegistry *registry, const char *name);
size_t im_type_registry_count(const ImTypeRegistry *registry);
ImEnum *im_type_registry_enum(const ImTypeRegistry *registry, const char *name);

#endif
