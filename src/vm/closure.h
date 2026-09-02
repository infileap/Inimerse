#ifndef INIMERSE_CLOSURE_H
#define INIMERSE_CLOSURE_H

#include "vm.h"
#include <stddef.h>

typedef struct ImClosureEnv ImClosureEnv;
typedef struct ImClosureFunction ImClosureFunction;
ImClosureEnv *im_closure_env_new(size_t slots);
ImClosureEnv *im_closure_env_clone(const ImClosureEnv *source);
void im_closure_env_retain(ImClosureEnv *env);
void im_closure_env_release(ImClosureEnv *env);
size_t im_closure_env_size(const ImClosureEnv *env);
int im_closure_env_set(ImClosureEnv *env, size_t index, const Value *value);
const Value *im_closure_env_get(const ImClosureEnv *env, size_t index);
ImClosureFunction *im_closure_function_new(int function_index, ImClosureEnv *env);
void im_closure_function_retain(ImClosureFunction *fn);
void im_closure_function_release(ImClosureFunction *fn);
int im_closure_function_index(const ImClosureFunction *fn);
ImClosureEnv *im_closure_function_env(const ImClosureFunction *fn);

#endif
