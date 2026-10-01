/* json_min.h - minimal strict JSON value tree for the Layer (P1)
 *
 * Deliberately standalone: no VM, no platform code.  Integers only, because
 * the canonical writer in eventlog.c only ever emits integers, so every
 * record the Layer writes can be parsed back byte-exactly.
 */
#ifndef INIMERSE_VERSE_JSON_MIN_H
#define INIMERSE_VERSE_JSON_MIN_H

#include <stddef.h>

typedef enum { VJ_NULL = 0, VJ_BOOL, VJ_INT, VJ_STR, VJ_ARR, VJ_OBJ } VjType;

typedef struct VjVal VjVal;
struct VjVal {
    VjType       type;
    int          b;      /* VJ_BOOL */
    long long    i;      /* VJ_INT  */
    char        *s;      /* VJ_STR  */
    VjVal      **items;  /* VJ_ARR  */
    size_t       n;
    char       **keys;   /* VJ_OBJ  */
    VjVal      **vals;
    size_t       nkv;
};

/* Parse NUL-terminated JSON.  Returns NULL on error, writing a short
 * description into err (when errlen > 0). */
VjVal *vj_parse(const char *text, char *err, size_t errlen);
void   vj_free(VjVal *v);

/* Object member lookup; NULL when absent or when v is not an object. */
const VjVal *vj_get(const VjVal *v, const char *key);

long long   vj_int(const VjVal *v, long long dflt);
int         vj_bool(const VjVal *v, int dflt);
const char *vj_str(const VjVal *v, const char *dflt);

#endif /* INIMERSE_VERSE_JSON_MIN_H */
