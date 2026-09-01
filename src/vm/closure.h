#ifndef INIMERSE_CLOSURE_H
#define INIMERSE_CLOSURE_H

#include "vm.h"
#include <stddef.h>

typedef struct ImClosureEnv ImClosureEnv;
ImClosureEnv *im_closure_env_new(size_t slots);
void im_closure_env_retain(ImClosureEnv *env);
void im_closure_env_release(ImClosureEnv *env);
size_t im_closure_env_size(const ImClosureEnv *env);
int im_closure_env_set(ImClosureEnv *env, size_t index, const Value *value);
const Value *im_closure_env_get(const ImClosureEnv *env, size_t index);

#endif
