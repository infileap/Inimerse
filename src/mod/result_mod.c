#include "mod.h"
#include "../vm/vm.h"
#include <string.h>

static Value key(const char *s) { Value v = { VAL_STRING, 0, 0, (char*)s }; return v; }
static Value make_result(VM *vm, int is_ok, const Value *payload) {
    Value out = { VAL_NIL, 0, 0, NULL }; int a = vm_array_new(vm); if (a < 0) return out;
    Value k_ok = key("ok"), k_val = key(is_ok ? "value" : "error"), flag = { VAL_BOOL, is_ok, 0, NULL };
    vm_dict_set(vm, a, &k_ok, &flag); vm_dict_set(vm, a, &k_val, payload); out.type = VAL_DICT; out.ival = a + 1; return out;
}

static int result_make_ok(VM *vm) {
    if (vm_cur_sp(vm) < 0) { push_nil(vm); return 1; }
    Value p = vm_cur_stack(vm)[vm_cur_sp(vm)]; pop(vm); Value r = make_result(vm, 1, &p);
    vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = r; return 1;
}
static int result_make_err(VM *vm) {
    if (vm_cur_sp(vm) < 0) { push_nil(vm); return 1; }
    Value p = vm_cur_stack(vm)[vm_cur_sp(vm)]; pop(vm); Value r = make_result(vm, 0, &p);
    vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = r; return 1;
}
static int result_is_ok(VM *vm) {
    if (vm_cur_sp(vm) < 0) { push_bool(vm, false); return 1; }
    Value r = vm_cur_stack(vm)[vm_cur_sp(vm)]; pop(vm); Value k = key("ok"); Value v = r.type == VAL_DICT ? vm_dict_get(vm, r.ival - 1, &k) : (Value){VAL_BOOL, 0, 0, NULL};
    push_bool(vm, v.type == VAL_BOOL && v.ival != 0); return 1;
}
static int result_unwrap_or(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value fallback = vm_cur_stack(vm)[vm_cur_sp(vm)], r = vm_cur_stack(vm)[vm_cur_sp(vm)-1]; vm_cur_set_sp(vm, vm_cur_sp(vm)-2);
    Value ko = key("ok"), kv = key("value"); Value ok = r.type == VAL_DICT ? vm_dict_get(vm, r.ival - 1, &ko) : (Value){VAL_BOOL, 0, 0, NULL};
    Value out = (ok.type == VAL_BOOL && ok.ival) ? vm_dict_get(vm, r.ival - 1, &kv) : fallback;
    vm_cur_set_sp(vm, vm_cur_sp(vm)+1); vm_cur_stack(vm)[vm_cur_sp(vm)] = out; return 1;
}

void result_mod_register(VM *vm) {
    vm_register_builtin(vm, "ok", result_make_ok); vm_register_builtin(vm, "err", result_make_err);
    vm_register_builtin(vm, "is_ok", result_is_ok); vm_register_builtin(vm, "unwrap_or", result_unwrap_or);
}
