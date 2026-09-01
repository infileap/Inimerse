#ifndef INIMERSE_ERROR_TYPES_H
#define INIMERSE_ERROR_TYPES_H

#include <stddef.h>
#include "typeset.h"
#include "enum.h"

typedef enum {
    IM_ERROR_DOMAIN_FILE = 0,
    IM_ERROR_DOMAIN_PARSE,
    IM_ERROR_DOMAIN_ARITHMETIC_VM,
    IM_ERROR_DOMAIN_MEMORY_VM,
    IM_ERROR_DOMAIN_TYPE_VM,
    IM_ERROR_DOMAIN_RUNTIME_VM
} ImErrorDomain;

typedef struct {
    const char *name;
    ImErrorDomain domain;
    int code;
} ImErrorKind;

const char *im_error_domain_name(ImErrorDomain domain);

const ImErrorKind *im_error_kind_lookup(const char *name);
const ImErrorKind *im_error_kind_at(size_t index);
size_t im_error_kind_count(void);
int im_error_kind_in_domain(const ImErrorKind *kind, ImErrorDomain domain);
ImTypeSet *im_error_domain_set(ImErrorDomain domain);
/* Symbolic finite-enum descriptor for a preset error domain. */
ImEnum *im_error_domain_enum(ImErrorDomain domain);

#endif
