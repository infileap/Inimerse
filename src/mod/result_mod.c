#include "mod.h"
#include "../vm/vm.h"
#include <string.h>
#include <stdio.h>

static Value key(const char *s) { Value v = { VAL_STRING, 0, 0, (char*)s }; return v; }
static Value make_result(VM *vm, int is_ok, const Value *payload) {
    Value out = { VAL_NIL, 0, 0, NULL }; int a = vm_array_new(vm); if (a < 0) return out;
    Value k_ok = key("ok"), k_val = key(is_ok ? "value" : "error"), flag = { VAL_BOOL, is_ok, 0, NULL };
    vm_dict_set(vm, a, &k_ok, &flag); vm_dict_set(vm, a, &k_val, payload); out.type = VAL_DICT; out.ival = a + 1; return out;
}

static int result_make_ok(VM *vm) {
    if (vm_cur_sp(vm) < 0) { push_nil(vm); return 1; }
    Value *p = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value r = make_result(vm, 1, p);
    pop(vm);
    vm_push_value(vm, &r);
    return 1;
}
static int result_make_err(VM *vm) {
    if (vm_cur_sp(vm) < 0) { push_nil(vm); return 1; }
    Value *p = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value r = make_result(vm, 0, p);
    pop(vm);
    vm_push_value(vm, &r);
    return 1;
}
static int result_is_ok(VM *vm) {
    if (vm_cur_sp(vm) < 0) { push_bool(vm, false); return 1; }
    Value r = vm_cur_stack(vm)[vm_cur_sp(vm)]; pop(vm); Value k = key("ok"); Value v = r.type == VAL_DICT ? vm_dict_get(vm, r.ival - 1, &k) : (Value){VAL_BOOL, 0, 0, NULL};
    push_bool(vm, v.type == VAL_BOOL && v.ival != 0); return 1;
}
static int result_unwrap_or(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value *stack = vm_cur_stack(vm);
    int sp = vm_cur_sp(vm);
    Value fallback = { VAL_NIL, 0, 0, NULL, NULL };
    Value r = { VAL_NIL, 0, 0, NULL, NULL };
    vm_value_assign(&fallback, &stack[sp]);
    vm_value_assign(&r, &stack[sp - 1]);
    Value ko = key("ok"), kv = key("value"); Value ok = r.type == VAL_DICT ? vm_dict_get(vm, r.ival - 1, &ko) : (Value){VAL_BOOL, 0, 0, NULL};
    Value out = { VAL_NIL, 0, 0, NULL, NULL };
    if (ok.type == VAL_BOOL && ok.ival) {
        Value got = vm_dict_get(vm, r.ival - 1, &kv);
        vm_value_move(&out, &got);
        value_free(&fallback);
    }
    else vm_value_move(&out, &fallback);
    pop(vm);
    pop(vm);
    value_free(&r);
    vm_push_value(vm, &out);
    return 1;
}
static int result_unwrap(VM *vm) {
    if (vm_cur_sp(vm) < 0) { push_nil(vm); return 1; }
    Value r = vm_cur_stack(vm)[vm_cur_sp(vm)]; pop(vm); Value ko = key("ok"), kv = key("value"), ke = key("error");
    Value ok = r.type == VAL_DICT ? vm_dict_get(vm, r.ival - 1, &ko) : (Value){VAL_BOOL, 0, 0, NULL};
    if (ok.type == VAL_BOOL && ok.ival) {
        Value out = vm_dict_get(vm, r.ival - 1, &kv);
        vm_push_value(vm, &out);
    } else {
        Value e = r.type == VAL_DICT ? vm_dict_get(vm, r.ival - 1, &ke) : (Value){VAL_STRING, 1, 0, "unwrap of non-Result"};
        char msg[256];
        snprintf(msg, sizeof msg, "Result unwrap failed: %s", e.sval ? e.sval : "error");
        vm_throw_msg(vm, msg);
        value_free(&e);
        push_nil(vm);
    }
    return 1;
}
static int result_field(VM *vm, int want_error) {
    if (vm_cur_sp(vm) < 0) { push_nil(vm); return 1; }
    Value r = vm_cur_stack(vm)[vm_cur_sp(vm)]; pop(vm);
    Value k_ok = key("ok"), k_field = key(want_error ? "error" : "value");
    Value ok = r.type == VAL_DICT ? vm_dict_get(vm, r.ival - 1, &k_ok) : (Value){VAL_BOOL, 0, 0, NULL};
    int is_ok = ok.type == VAL_BOOL && ok.ival != 0;
    if ((want_error && is_ok) || (!want_error && !is_ok)) { push_nil(vm); return 1; }
    Value out = r.type == VAL_DICT ? vm_dict_get(vm, r.ival - 1, &k_field) : (Value){VAL_NIL, 0, 0, NULL};
    vm_push_value(vm, &out);
    return 1;
}
static int result_value(VM *vm) { return result_field(vm, 0); }
static int result_error(VM *vm) { return result_field(vm, 1); }
static int result_dict_has(VM *vm) {
    if (vm_cur_sp(vm) < 1) { push_bool(vm, false); return 1; }
    Value keyv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value obj = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    int found = obj.type == VAL_DICT && vm_dict_has(vm, obj.ival - 1, &keyv);
    pop(vm);
    pop(vm);
    push_bool(vm, found);
    return 1;
}

static int result_thread_result(VM *vm) {
    if (vm_cur_sp(vm) < 0) {
        Value msg = { VAL_STRING, 0, 0, "thread_result requires a thread name", NULL };
        Value out = make_result(vm, 0, &msg);
        vm_push_value(vm, &out);
        value_free(&out);
        return 1;
    }
    Value name_value = vm_cur_stack(vm)[vm_cur_sp(vm)];
    char name[512];
    if (name_value.type != VAL_STRING || !name_value.sval) {
        pop(vm);
        Value msg = { VAL_STRING, 0, 0, "thread_result requires a string name", NULL };
        Value out = make_result(vm, 0, &msg);
        vm_push_value(vm, &out);
        value_free(&out);
        return 1;
    }
    snprintf(name, sizeof(name), "%s", name_value.sval);
    pop(vm);

    Value payload = { VAL_NIL, 0, 0, NULL, NULL };
    int status = vm_thread_completion(vm, name, &payload);
    if (status == 0) {
        Value msg = { VAL_STRING, 0, 0, "thread is missing or not finished", NULL };
        Value out = make_result(vm, 0, &msg);
        vm_push_value(vm, &out);
        value_free(&out);
        return 1;
    }
    if (status == 1) {
        Value k_ok = key("ok");
        Value marker = payload.type == VAL_DICT
            ? vm_dict_get(vm, payload.ival - 1, &k_ok)
            : (Value){ VAL_NIL, 0, 0, NULL, NULL };
        if (marker.type == VAL_BOOL) {
            vm_push_value(vm, &payload);
        } else {
            Value out = make_result(vm, 1, &payload);
            vm_push_value(vm, &out);
            value_free(&out);
        }
    } else {
        Value out = make_result(vm, 0, &payload);
        vm_push_value(vm, &out);
        value_free(&out);
    }
    value_free(&payload);
    return 1;
}

static int result_thread_await(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 1) {
        Value msg = { VAL_STRING, 0, 0, "thread_await requires a thread name", NULL };
        Value out = make_result(vm, 0, &msg);
        vm_push_value(vm, &out);
        value_free(&out);
        return 1;
    }
    Value name_value = vm_cur_stack(vm)[argc - 1];
    long long timeout_ms = -1;
    if (argc >= 2) {
        Value tv = vm_cur_stack(vm)[argc - 1];
        name_value = vm_cur_stack(vm)[argc - 2];
        double seconds = tv.type == VAL_INT ? (double)tv.ival :
                         tv.type == VAL_FLOAT ? tv.fval : 0.0;
        timeout_ms = seconds < 0 ? -1 : (long long)(seconds * 1000.0);
    }
    char name[512] = "";
    if (name_value.type == VAL_STRING && name_value.sval)
        snprintf(name, sizeof(name), "%s", name_value.sval);
    while (vm_cur_sp(vm) >= 0) pop(vm);
    if (!name[0]) {
        Value msg = { VAL_STRING, 0, 0, "thread_await requires a string name", NULL };
        Value out = make_result(vm, 0, &msg);
        vm_push_value(vm, &out);
        value_free(&out);
        return 1;
    }
    if (!vm_thread_wait(vm, name, timeout_ms)) {
        Value msg = { VAL_STRING, 0, 0, "thread await timed out or thread is missing", NULL };
        Value out = make_result(vm, 0, &msg);
        vm_push_value(vm, &out);
        value_free(&out);
        return 1;
    }
    Value payload = { VAL_NIL, 0, 0, NULL, NULL };
    int status = vm_thread_completion(vm, name, &payload);
    if (status == 1) {
        Value marker_key = key("ok");
        Value marker = payload.type == VAL_DICT
            ? vm_dict_get(vm, payload.ival - 1, &marker_key)
            : (Value){ VAL_NIL, 0, 0, NULL, NULL };
        if (marker.type == VAL_BOOL) {
            vm_push_value(vm, &payload);
        } else {
            Value out = make_result(vm, 1, &payload);
            vm_push_value(vm, &out);
            value_free(&out);
        }
    } else if (status == 2) {
        Value out = make_result(vm, 0, &payload);
        vm_push_value(vm, &out);
        value_free(&out);
    } else {
        Value msg = { VAL_STRING, 0, 0, "thread completed value is unavailable", NULL };
        Value out = make_result(vm, 0, &msg);
        vm_push_value(vm, &out);
        value_free(&out);
    }
    value_free(&payload);
    return 1;
}

static int result_thread_release(VM *vm) {
    if (vm_cur_sp(vm) < 0) {
        push_bool(vm, false);
        return 1;
    }
    Value name_value = vm_cur_stack(vm)[vm_cur_sp(vm)];
    int released = 0;
    if (name_value.type == VAL_STRING && name_value.sval)
        released = vm_thread_release(vm, name_value.sval);
    pop(vm);
    push_bool(vm, released != 0);
    return 1;
}

void result_mod_register(VM *vm) {
    vm_register_builtin(vm, "ok", result_make_ok); vm_register_builtin(vm, "err", result_make_err);
    vm_register_builtin(vm, "is_ok", result_is_ok); vm_register_builtin(vm, "unwrap_or", result_unwrap_or); vm_register_builtin(vm, "unwrap", result_unwrap);
    vm_register_builtin(vm, "result_value", result_value);
    vm_register_builtin(vm, "result_error", result_error);
    vm_register_builtin(vm, "dict_has", result_dict_has);
    vm_register_builtin(vm, "thread_result", result_thread_result);
    vm_register_builtin(vm, "thread_await", result_thread_await);
    vm_register_builtin(vm, "thread_release", result_thread_release);
}
