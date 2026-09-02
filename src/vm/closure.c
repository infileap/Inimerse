#include "closure.h"
#include <stdlib.h>

struct ImClosureEnv {
    size_t slots;
    size_t refs;
    Value *values;
};
struct ImClosureFunction { int function_index; size_t refs; ImClosureEnv *env; };

ImClosureEnv *im_closure_env_new(size_t slots) {
    ImClosureEnv *e = (ImClosureEnv *)calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->slots = slots; e->refs = 1;
    e->values = slots ? (Value *)calloc(slots, sizeof(Value)) : NULL;
    if (slots && !e->values) { free(e); return NULL; }
    for (size_t i = 0; i < slots; ++i) e->values[i].type = VAL_NIL;
    return e;
}
ImClosureEnv *im_closure_env_clone(const ImClosureEnv *source) {
    if (!source) return NULL;
    ImClosureEnv *copy = im_closure_env_new(source->slots);
    if (!copy) return NULL;
    for (size_t i = 0; i < source->slots; ++i) copy->values[i] = source->values[i];
    return copy;
}
void im_closure_env_retain(ImClosureEnv *e) { if (e) ++e->refs; }
void im_closure_env_release(ImClosureEnv *e) { if (e && --e->refs == 0) { free(e->values); free(e); } }
size_t im_closure_env_size(const ImClosureEnv *e) { return e ? e->slots : 0; }
int im_closure_env_set(ImClosureEnv *e, size_t i, const Value *v) { if (!e || !v || i >= e->slots) return 0; e->values[i] = *v; return 1; }
void im_closure_env_clear(ImClosureEnv *e) {
    if (!e) return;
    for (size_t i = 0; i < e->slots; ++i) {
        e->values[i].type = VAL_NIL; e->values[i].ival = 0; e->values[i].fval = 0; e->values[i].sval = NULL;
    }
}
const Value *im_closure_env_get(const ImClosureEnv *e, size_t i) { return e && i < e->slots ? &e->values[i] : NULL; }

ImClosureFunction *im_closure_function_new(int index, ImClosureEnv *env) {
    if (index < 0) return NULL;
    ImClosureFunction *fn = (ImClosureFunction *)calloc(1, sizeof(*fn));
    if (!fn) return NULL;
    fn->function_index = index; fn->refs = 1; fn->env = env;
    if (env) im_closure_env_retain(env);
    return fn;
}
void im_closure_function_retain(ImClosureFunction *fn) { if (fn) ++fn->refs; }
void im_closure_function_release(ImClosureFunction *fn) { if (fn && --fn->refs == 0) { im_closure_env_release(fn->env); free(fn); } }
int im_closure_function_index(const ImClosureFunction *fn) { return fn ? fn->function_index : -1; }
ImClosureEnv *im_closure_function_env(const ImClosureFunction *fn) { return fn ? fn->env : NULL; }
