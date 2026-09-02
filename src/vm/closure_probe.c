#include "closure.h"
#include <assert.h>

int main(void) {
    ImClosureEnv *e = im_closure_env_new(2);
    assert(e && im_closure_env_size(e) == 2);
    Value v = {.type = VAL_INT, .ival = 42};
    assert(im_closure_env_set(e, 0, &v) && im_closure_env_get(e, 0)->ival == 42);
    ImClosureEnv *copy = im_closure_env_clone(e);
    assert(copy && im_closure_env_get(copy, 0)->ival == 42 && copy != e);
    im_closure_env_release(copy);
    assert(!im_closure_env_set(e, 2, &v) && !im_closure_env_get(e, 2));
    im_closure_env_retain(e); im_closure_env_release(e); im_closure_env_release(e);
    ImClosureEnv *captured = im_closure_env_new(1);
    ImClosureFunction *fn = im_closure_function_new(7, captured);
    assert(fn && im_closure_function_index(fn) == 7 && im_closure_function_env(fn) == captured);
    im_closure_env_release(captured);
    im_closure_function_retain(fn); im_closure_function_release(fn); im_closure_function_release(fn);
    assert(!im_closure_function_new(-1, NULL));
    assert(!im_closure_env_clone(NULL));
    return 0;
}
