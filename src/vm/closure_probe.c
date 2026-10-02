#include "closure.h"
#include "../platform/thread.h"
#include <assert.h>

#ifdef _WIN32
static unsigned __stdcall churn(void *arg) {
#else
static void *churn(void *arg) {
#endif
    ImClosureEnv *env = (ImClosureEnv *)arg;
    for (int i = 0; i < 10000; ++i) { im_closure_env_retain(env); im_closure_env_release(env); }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int main(void) {
    ImClosureEnv *e = im_closure_env_new(2);
    assert(e && im_closure_env_size(e) == 2);
    assert(im_closure_env_refs(e) == 1);
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
    ImClosureEnv *captured = im_closure_env_new(1);
    ImClosureFunction *fn = im_closure_function_new(7, captured);
    assert(fn && im_closure_function_index(fn) == 7 && im_closure_function_env(fn) == captured);
    assert(im_closure_env_refs(captured) == 2);
    im_closure_env_release(captured);
    assert(im_closure_env_refs(im_closure_function_env(fn)) == 1);
    im_closure_function_retain(fn); im_closure_function_release(fn); im_closure_function_release(fn);
    assert(!im_closure_function_new(-1, NULL));
    assert(!im_closure_env_clone(NULL));
    /* An empty destination must refuse the copy.  The source here is a live
     * env, not `e`: `e` is freed by the balance check below, and passing a
     * freed env as the source meant this assertion read freed memory.  Under
     * Release (-DNDEBUG) the assertion is compiled out and the stale pointer
     * instead corrupted the heap -- "free(): invalid size" / "corrupted size
     * vs. prev_size", roughly 3 in 720 runs at twelve-way parallelism.  The
     * real fix is that `e` must not be released until every use is done. */
    ImClosureEnv *empty = im_closure_env_new(0);
    ImClosureEnv *src = im_closure_env_new(1);
    assert(empty && im_closure_env_size(empty) == 0);
    assert(src && im_closure_env_copy_slot(empty, 9, src, 0) == 0);
    assert(src && !im_closure_env_copy_slot(empty, 0, src, 0));
    im_closure_env_clear(empty);
    im_closure_env_release(empty);
    im_closure_env_release(src);
    /* Balanced retain/release must leave the count exactly where it started,
     * and the last release must actually free -- so this is the final use of
     * `e`, and it is deliberately the last reference. */
    im_closure_env_retain(e); im_closure_env_release(e); im_closure_env_release(e);
    ImClosureEnv *shared = im_closure_env_new(1);
    void *t1 = shared ? im_thread_start(churn, shared) : NULL;
    void *t2 = shared ? im_thread_start(churn, shared) : NULL;
    assert(t1 && t2);
    assert(im_thread_join(t1, 5000) == 0 && im_thread_join(t2, 5000) == 0);
    im_thread_close(t1); im_thread_close(t2);
    im_closure_env_release(shared);
    return 0;
}
