#include "closure.h"
#include <stdlib.h>

struct ImClosureEnv {
    size_t slots;
    size_t refs;
    Value *values;
};

ImClosureEnv *im_closure_env_new(size_t slots) {
    ImClosureEnv *e = (ImClosureEnv *)calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->slots = slots; e->refs = 1;
    e->values = slots ? (Value *)calloc(slots, sizeof(Value)) : NULL;
    if (slots && !e->values) { free(e); return NULL; }
    for (size_t i = 0; i < slots; ++i) e->values[i].type = VAL_NIL;
    return e;
}
void im_closure_env_retain(ImClosureEnv *e) { if (e) ++e->refs; }
void im_closure_env_release(ImClosureEnv *e) { if (e && --e->refs == 0) { free(e->values); free(e); } }
size_t im_closure_env_size(const ImClosureEnv *e) { return e ? e->slots : 0; }
int im_closure_env_set(ImClosureEnv *e, size_t i, const Value *v) { if (!e || !v || i >= e->slots) return 0; e->values[i] = *v; return 1; }
const Value *im_closure_env_get(const ImClosureEnv *e, size_t i) { return e && i < e->slots ? &e->values[i] : NULL; }
