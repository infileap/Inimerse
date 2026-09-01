#include "error_types.h"

#include <stdlib.h>
#include <string.h>

static const ImErrorKind g_errors[] = {
    {"not_found", IM_ERROR_DOMAIN_FILE, 1001},
    {"permission_denied", IM_ERROR_DOMAIN_FILE, 1002},
    {"disk_full", IM_ERROR_DOMAIN_FILE, 1003},
    {"invalid_path", IM_ERROR_DOMAIN_FILE, 1004},
    {"invalid_format", IM_ERROR_DOMAIN_PARSE, 1101},
    {"unexpected_token", IM_ERROR_DOMAIN_PARSE, 1102},
    {"division_by_zero", IM_ERROR_DOMAIN_ARITHMETIC_VM, 2001},
    {"numeric_overflow", IM_ERROR_DOMAIN_ARITHMETIC_VM, 2002},
    {"invalid_numeric_operation", IM_ERROR_DOMAIN_ARITHMETIC_VM, 2003},
    {"index_out_of_range", IM_ERROR_DOMAIN_MEMORY_VM, 2101},
    {"allocation_failed", IM_ERROR_DOMAIN_MEMORY_VM, 2102},
    {"invalid_reference", IM_ERROR_DOMAIN_MEMORY_VM, 2103},
    {"value_not_callable", IM_ERROR_DOMAIN_TYPE_VM, 2201},
    {"type_mismatch", IM_ERROR_DOMAIN_TYPE_VM, 2202},
    {"invalid_conversion", IM_ERROR_DOMAIN_TYPE_VM, 2203},
    {"uncaught_exception", IM_ERROR_DOMAIN_RUNTIME_VM, 2301},
    {"instruction_limit", IM_ERROR_DOMAIN_RUNTIME_VM, 2302}
};

const char *im_error_domain_name(ImErrorDomain domain) {
    switch (domain) {
        case IM_ERROR_DOMAIN_FILE: return "FileError";
        case IM_ERROR_DOMAIN_PARSE: return "ParseError";
        case IM_ERROR_DOMAIN_ARITHMETIC_VM: return "ArithmeticVMError";
        case IM_ERROR_DOMAIN_MEMORY_VM: return "MemoryVMError";
        case IM_ERROR_DOMAIN_TYPE_VM: return "TypeVMError";
        case IM_ERROR_DOMAIN_RUNTIME_VM: return "RuntimeVMError";
        default: return "Error";
    }
}

const ImErrorKind *im_error_kind_lookup(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(g_errors) / sizeof(g_errors[0]); ++i)
        if (strcmp(g_errors[i].name, name) == 0) return &g_errors[i];
    return NULL;
}

const ImErrorKind *im_error_kind_at(size_t index) {
    return index < sizeof(g_errors) / sizeof(g_errors[0]) ? &g_errors[index] : NULL;
}

size_t im_error_kind_count(void) { return sizeof(g_errors) / sizeof(g_errors[0]); }

int im_error_kind_in_domain(const ImErrorKind *kind, ImErrorDomain domain) {
    return kind && kind->domain == domain;
}

ImTypeSet *im_error_domain_set(ImErrorDomain domain) {
    size_t count = 0;
    for (size_t i = 0; i < im_error_kind_count(); ++i)
        if (g_errors[i].domain == domain) ++count;
    ImTypeValue *values = count ? (ImTypeValue *)malloc(count * sizeof(*values)) : NULL;
    if (count && !values) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < im_error_kind_count(); ++i) {
        if (g_errors[i].domain != domain) continue;
        values[j].kind = IM_TYPE_STRING;
        values[j].string = g_errors[i].name;
        ++j;
    }
    ImTypeSet *set = im_typeset_enum(IM_TYPE_STRING, values, count);
    free(values);
    return set;
}

ImEnum *im_error_domain_enum(ImErrorDomain domain) {
    size_t count = 0;
    for (size_t i = 0; i < im_error_kind_count(); ++i)
        if (g_errors[i].domain == domain) ++count;
    const char **names = count ? (const char **)malloc(count * sizeof(*names)) : NULL;
    if (count && !names) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < im_error_kind_count(); ++i)
        if (g_errors[i].domain == domain) names[j++] = g_errors[i].name;
    ImEnum *result = im_enum_create(im_error_domain_name(domain), names, count);
    free(names);
    return result;
}
