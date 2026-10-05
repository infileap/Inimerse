/* vm_exec_builtin.c - the one implementation of the `vm_exec` builtin.
 *
 * `vm_exec(data)` is how a program written in Inimerse hands the VM a bytecode
 * image it built at run time; the self-hosted compiler (selfhost/compiler.im)
 * is the only thing in the tree that uses it, and it uses it for its entire
 * output path.
 *
 * This used to live in runtime.c, which CMakeLists.txt compiles only on WIN32.
 * The POSIX runtime (runtime_posix.c) re-implements the whole builtin surface
 * and had registered `vm_exec` as posix_unsupported -- so on Linux the
 * self-hosted compiler compiled a program, called vm_exec, got a silent -1
 * back and exited 0 with no output at all.  Both runtimes now register this
 * shared implementation, which is what runtime_posix.c's header comment asks
 * for: "fail explicitly until their backend is shared with this runtime".
 */
#include "runtime.h"
#include "bytecode.h"
#include <stdlib.h>
#include <string.h>

/* Rebuild a Bytecode from the dict shape the self-hosted compiler emits:
 * {"code": [[op,r1,r2,r3],...], "strings": [...], "floats": [...],
 *  "funcs": [{"n":..,"a":..,"code":..,"strings":..,"floats":..},...],
 *  "threads": [ ... same as funcs ... ]} */
static Bytecode *bc_from_data(VM *vm, Value data) {
    Bytecode *bc = calloc(1, sizeof(Bytecode));
    if (!bc) return NULL;
    bytecode_init(bc);
    Value key;
    key.type = VAL_STRING; key.ival = 0;  key.sval = "strings";
    Value strs = vm_dict_get(vm, data.ival - 1, &key);
    if (strs.type == VAL_ARRAY) {
        int n = vm_array_len(vm, strs.ival - 1);
        for (int i = 0; i < n; i++) {
            Value s = vm_array_get(vm, strs.ival - 1, i);
            bc->string_pool = realloc(bc->string_pool, (bc->string_count + 1) * sizeof(char*));
            bc->string_pool[bc->string_count++] = (s.type == VAL_STRING && s.sval) ? strdup(s.sval) : strdup("");
        }
    }
    key.sval = "floats";
    Value fls = vm_dict_get(vm, data.ival - 1, &key);
    if (fls.type == VAL_ARRAY) {
        int n = vm_array_len(vm, fls.ival - 1);
        for (int i = 0; i < n; i++) {
            Value f = vm_array_get(vm, fls.ival - 1, i);
            bc->float_pool = realloc(bc->float_pool, (bc->float_count + 1) * sizeof(double));
            bc->float_pool[bc->float_count++] = (f.type == VAL_FLOAT) ? f.fval : 0.0;
        }
    }
    key.sval = "code";
    Value cd = vm_dict_get(vm, data.ival - 1, &key);
    if (cd.type == VAL_ARRAY) {
        int n = vm_array_len(vm, cd.ival - 1);
        for (int i = 0; i < n; i++) {
            Value ins = vm_array_get(vm, cd.ival - 1, i);
            OpCode op = 0; int r1 = 0, r2 = 0, r3 = 0;
            if (ins.type == VAL_ARRAY) {
                int m = vm_array_len(vm, ins.ival - 1);
                Value x;
                if (m > 0) { x = vm_array_get(vm, ins.ival - 1, 0); op = (OpCode)((x.type == VAL_INT) ? x.ival : 0); }
                if (m > 1) { x = vm_array_get(vm, ins.ival - 1, 1); r1 = (x.type == VAL_INT) ? x.ival : 0; }
                if (m > 2) { x = vm_array_get(vm, ins.ival - 1, 2); r2 = (x.type == VAL_INT) ? x.ival : 0; }
                if (m > 3) { x = vm_array_get(vm, ins.ival - 1, 3); r3 = (x.type == VAL_INT) ? x.ival : 0; }
            }
            bytecode_add(bc, op, r1, r2, r3);
        }
    }
    key.sval = "funcs";
    Value funcs = vm_dict_get(vm, data.ival - 1, &key);
    if (funcs.type == VAL_ARRAY) {
        int n = vm_array_len(vm, funcs.ival - 1);
        for (int i = 0; i < n && bc->func_count < 64; i++) {
            Value f = vm_array_get(vm, funcs.ival - 1, i);
            if (f.type != VAL_DICT) continue;
            Bytecode *fb = bc_from_data(vm, f);
            if (!fb) continue;
            bc->funcs[bc->func_count] = fb;
            Value nk; nk.type = VAL_STRING; nk.ival = 0;  nk.sval = "n";
            Value nm = vm_dict_get(vm, f.ival - 1, &nk);
            bc->func_names[bc->func_count] = (nm.type == VAL_STRING && nm.sval) ? strdup(nm.sval) : strdup("func");
            Value ak; ak.type = VAL_STRING; ak.ival = 0;  ak.sval = "a";
            Value av = vm_dict_get(vm, f.ival - 1, &ak);
            bc->func_argc[bc->func_count] = (av.type == VAL_INT) ? av.ival : 0;
            bc->func_count++;
        }
    }
    /* threads -- thread bytecode */
    key.sval = "threads";
    Value ths = vm_dict_get(vm, data.ival - 1, &key);
    if (ths.type == VAL_ARRAY) {
        int n = vm_array_len(vm, ths.ival - 1);
        for (int i = 0; i < n && bc->thread_count < 32; i++) {
            Value f = vm_array_get(vm, ths.ival - 1, i);
            if (f.type != VAL_DICT) continue;
            Bytecode *tb = bc_from_data(vm, f);
            if (!tb) continue;
            bc->threads[bc->thread_count] = tb;
            Value nk2; nk2.type = VAL_STRING; nk2.ival = 0;  nk2.sval = "n";
            Value nm2 = vm_dict_get(vm, f.ival - 1, &nk2);
            bc->thread_names[bc->thread_count] = (nm2.type == VAL_STRING && nm2.sval) ? strdup(nm2.sval) : strdup("th");
            Value ak2; ak2.type = VAL_STRING; ak2.ival = 0;  ak2.sval = "a";
            Value av2 = vm_dict_get(vm, f.ival - 1, &ak2);
            bc->thread_argc[bc->thread_count] = (av2.type == VAL_INT) ? av2.ival : 0;
            bc->thread_count++;
        }
    }
    return bc;
}

int im_builtin_vm_exec(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value data = vm_cur_stack(vm)[vm_cur_sp(vm)];
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    if (data.type != VAL_DICT) { push_int(vm, 0); return 1; }
    Bytecode *bc = bc_from_data(vm, data);
    if (!bc) { push_int(vm, 0); return 1; }
    /* Save the globals; note that sizeof(vm->globals) is not the size of the
     * Value array -- declaring one on the stack would overflow and corrupt it. */
    GlobalSlot *saved_globals = vm->globals;
    int saved_gc = vm->globalCount;
    int saved_cap = vm->globalCap;
    int *saved_be = vm->global_bound;
    int saved_be_cap = vm->global_bound_cap;
    VmThread *saved_t = vm_get_cur_thread();
    Bytecode *saved_code = vm->code;
    vm_global_clone(vm);   /* swaps in an independent copy and leaves saved_globals untouched */
    vm_load_bytecode(vm, bc);
    vm_run(vm);
    vm_set_cur_thread(saved_t);
    vm->code = saved_code;
    for (int i = 0; i < vm->globalCount; i++) value_free(&vm->globals[i].val);
    for (int i = 0; i < vm->globalCount; i++) free(vm->globals[i].name);
    free(vm->globals);
    free(vm->global_bound);
    vm->globals = saved_globals;
    vm->globalCount = saved_gc;
    vm->globalCap = saved_cap;
    vm->global_bound = saved_be;
    vm->global_bound_cap = saved_be_cap;
    bytecode_free(bc);
    free(bc);
    push_int(vm, 1);
    return 1;
}
