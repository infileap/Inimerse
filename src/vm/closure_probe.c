#include "closure.h"
#include <assert.h>
#include <threads.h>

static int churn(void *arg) {
    ImClosureEnv *env = (ImClosureEnv *)arg;
    for (int i = 0; i < 10000; ++i) { im_closure_env_retain(env); im_closure_env_release(env); }
    return 0;
}

int main(void) {
    ImClosureEnv *e = im_closure_env_new(2);
    assert(e && im_closure_env_size(e) == 2);
    Value v = {.type = VAL_INT, .ival = 42};
    assert(im_closure_env_set(e, 0, &v) && im_closure_env_get(e, 0)->ival == 42);
    Value text = {.type = VAL_STRING, .sval = "captured"};
    assert(im_closure_env_set(e, 1, &text));
    ImClosureEnv *copy = im_closure_env_clone(e);
    assert(copy && im_closure_env_get(copy, 0)->ival == 42 && copy != e);
    assert(im_closure_env_get(copy, 1)->sval && im_closure_env_get(copy, 1)->sval != im_closure_env_get(e, 1)->sval);
    ImClosureEnv *slot_copy = im_closure_env_new(1);
    assert(slot_copy && im_closure_env_copy_slot(slot_copy, 0, e, 1));
    assert(im_closure_env_get(slot_copy, 0)->sval && im_closure_env_get(slot_copy, 0)->sval != im_closure_env_get(e, 1)->sval);
    im_closure_env_release(slot_copy);
    im_closure_env_release(copy);
    im_closure_env_clear(e);
    assert(im_closure_env_get(e, 0)->type == VAL_NIL);
    assert(!im_closure_env_set(e, 2, &v) && !im_closure_env_get(e, 2));
    im_closure_env_retain(e); im_closure_env_release(e); im_closure_env_release(e);
    ImClosureEnv *captured = im_closure_env_new(1);
    ImClosureFunction *fn = im_closure_function_new(7, captured);
    assert(fn && im_closure_function_index(fn) == 7 && im_closure_function_env(fn) == captured);
    im_closure_env_release(captured);
    im_closure_function_retain(fn); im_closure_function_release(fn); im_closure_function_release(fn);
    assert(!im_closure_function_new(-1, NULL));
    assert(!im_closure_env_clone(NULL));
    ImClosureEnv *shared = im_closure_env_new(1);
    thrd_t t1, t2;
    assert(shared && thrd_create(&t1, churn, shared) == thrd_success && thrd_create(&t2, churn, shared) == thrd_success);
    thrd_join(t1, NULL); thrd_join(t2, NULL);
    im_closure_env_release(shared);
    return 0;
}
