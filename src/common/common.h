#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

/* 动态数组模板 */
#define DECL_ARRAY(T) \
    typedef struct { \
        T *data; \
        int cap; \
        int len; \
    } T##Array; \
    static inline void T##Array_init(T##Array *arr) { \
        arr->data = NULL; arr->cap = arr->len = 0; \
    } \
    static inline void T##Array_push(T##Array *arr, T elem) { \
        if (arr->len >= arr->cap) { \
            arr->cap = arr->cap == 0 ? 8 : arr->cap * 2; \
            arr->data = realloc(arr->data, arr->cap * sizeof(T)); \
        } \
        arr->data[arr->len++] = elem; \
    } \
    static inline void T##Array_free(T##Array *arr) { \
        free(arr->data); \
        arr->data = NULL; arr->cap = arr->len = 0; \
    }

/* 字符串视图 */
typedef struct {
    const char *start;
    int length;
} StringView;

static inline StringView sv_from_cstr(const char *s) {
    return (StringView){ s, s ? (int)strlen(s) : 0 };
}

static inline bool sv_eq(StringView a, StringView b) {
    if (a.length != b.length) return false;
    return strncmp(a.start, b.start, a.length) == 0;
}

static inline bool sv_eq_cstr(StringView a, const char *s) {
    return sv_eq(a, sv_from_cstr(s));
}

/* AI-era structured errors (--err-json): defined in parser.c, set by main.c */
extern int g_err_json;
/* UTF-8 unified text loading: BOM strip + GBK(cp936)->UTF-8 transcode. Implemented in main.c. */
char *inim_load_text(const char *path);


#endif /* COMMON_H */
/* The release build injects this from CMake/CPack so CLI and package versions
 * cannot drift. Keep a source-tree fallback for non-CMake consumers. */
#ifndef INIMERSE_VERSION_STRING
#define INIMERSE_VERSION_STRING "0.4.0"
#endif
#define INFIVERSE_VERSION INIMERSE_VERSION_STRING
