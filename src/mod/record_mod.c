/* record_mod.c - record system: local save/load + sync primitives
 *
 * builtins:
 *   save([path])                      save all record vars to file
 *   load([path])                      load file into record vars (runtime)
 *   record_dirty()                    -> [names] with pending changes
 *   record_mark_clean(name)           clear dirty flag
 *   record_value(name)                -> current value
 *   record_set_value(name, value)     apply external (server-authoritative) value
 *   record_get(name)                  -> {store, scope, merge, version}
 *   record_snapshot()                 -> {name: value} for all record vars
 */
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern void json_write_value(VM *vm, const Value *v, char *out, int *pos, int outsz);
extern Value json_parse_value_text(VM *vm, const char *s, int *ok);
extern void json_write_value_dyn(VM *vm, const Value *v, char **buf, int *pos, int *cap);

/* ---- arg helpers (io-style: arg(0) = first arg) ---- */
static Value r_arg(VM *vm, int i) { return vm_cur_stack(vm)[vm_cur_sp(vm) - i]; }
static double r_arg_num(VM *vm, int i) { Value v = r_arg(vm, i); return (v.type == VAL_INT) ? (double)v.ival : (v.type == VAL_FLOAT) ? v.fval : 0.0; }
static const char *r_arg_str(VM *vm, int i) { Value v = r_arg(vm, i); return (v.type == VAL_STRING && v.sval) ? v.sval : ""; }
static void r_popn(VM *vm, int n) {
    while (n-- >0 && vm_cur_sp(vm) >=0) {
        Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
        if (v.type == VAL_STRING && v.ival !=1 && v.sval) free(v.sval);
        vm_cur_set_sp(vm, vm_cur_sp(vm) -1);
    }
}
static void r_push(VM *vm, Value v) { if (vm_cur_sp(vm) <1023) { vm_cur_set_sp(vm, vm_cur_sp(vm) +1); vm_cur_stack(vm)[vm_cur_sp(vm)] = v; } }
static void r_push_int(VM *vm, int n) { Value v; v.type = VAL_INT; v.ival = n; v.fval =0; v.sval = NULL; r_push(vm, v); }
static void r_push_nil(VM *vm) { Value v; v.type = VAL_NIL; v.ival =0; v.fval =0; v.sval = NULL; r_push(vm, v); }

static void r_copy_value(Value *dst, const Value *src) {
    *dst = *src;
    if (src->type == VAL_STRING && src->sval && src->ival !=1) dst->sval = strdup(src->sval);
}
static void r_free_value(Value *v) {
    if (v->type == VAL_STRING && v->ival !=1 && v->sval) free(v->sval);
    v->type = VAL_NIL; v->sval = NULL;
}

/* find record global index by name; -1 if not a record var */
static int record_find_idx(VM *vm, const char *name) {
    for (int i =0; i < vm->record_meta_count; i++) {
        if (vm->record_names && vm->record_names[i] && strcmp(vm->record_names[i], name) ==0) return i;
    }
    return -1;
}

/* ---- startup load: parse save file into vm->record_loaded_dict ---- */
void record_load_from_file(VM *vm, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f,0,SEEK_END); long len = ftell(f); fseek(f,0,SEEK_SET);
    if (len <=0 || len > (1<<20)) { fclose(f); return; }
    char *buf = malloc((size_t)len +1);
    fread(buf,1,(size_t)len,f); buf[len] = '\0';
    fclose(f);
    int ok =0;
    Value d = json_parse_value_text(vm, buf, &ok);
    free(buf);
    if (ok && d.type == VAL_DICT && d.ival >0) {
        vm->record_loaded_dict = d.ival; /* dict slot lives in the array pool */
    }
}

/* ---- save all record vars as {name: value} JSON ---- */
static void record_save_to_dict(VM *vm, Value *out) {
    int aidx = vm_array_new(vm);
    if (aidx <0) { out->type = VAL_NIL; out->ival =0; out->fval =0; out->sval = NULL; return; }
    for (int i =0; i < vm->record_meta_count; i++) {
        if (!(vm->record_names && vm->record_names[i])) continue;
        if (i >= vm->globalCount) continue;
        Value k; k.type = VAL_STRING; k.ival =0; k.fval =0; k.sval = strdup(vm->record_names[i]);
        Value v = vm->globals[i].val;
        vm_dict_set(vm, aidx, &k, &v);
        free(k.sval);
    }
    out->type = VAL_DICT; out->ival = aidx +1; out->fval =0; out->sval = NULL;
}

void record_save_to_file(VM *vm, const char *path) {
    if (vm->record_meta_count <=0) return;
    Value d;
    record_save_to_dict(vm, &d);
    if (d.type != VAL_DICT) return;
    int cap =1<<20;
    char *buf = malloc((size_t)cap);
    int pos =0;
    json_write_value_dyn(vm, &d, &buf, &pos, &cap);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(buf,1,(size_t)pos,f); fclose(f); }
    free(buf);
}

/* ---- builtins ---- */
/* ---- sprite tag tables (collectibles grouping) ---- */
#define TAG_MAX 64
#define TAG_ITEMS_MAX 256
static char g_tag_name[TAG_MAX][64];
static char g_tag_items[TAG_MAX][TAG_ITEMS_MAX][64];
static int g_tag_count[TAG_MAX];
static int g_tag_n = 0;

static int builtin_tag_register(VM *vm) {
    int argc = vm->cur_argc;
    if (argc < 1) { r_popn(vm, argc); r_push_int(vm, 0); return 1; }
    const char *name = argc >= 1 ? r_arg_str(vm, argc - 1) : ""; /* first arg = tag name */
    if (g_tag_n < TAG_MAX) {
        snprintf(g_tag_name[g_tag_n], 64, "%s", name);
        g_tag_count[g_tag_n] = 0;
        for (int i = 1; i < argc; i++) { /* items follow, reversed on stack */
            if (g_tag_count[g_tag_n] < TAG_ITEMS_MAX)
                snprintf(g_tag_items[g_tag_n][g_tag_count[g_tag_n]++], 64, "%s", r_arg_str(vm, argc - 1 - i));
        }
        g_tag_n++;
    }
    r_popn(vm, argc);
    r_push_int(vm, 1);
    return 1;
}
static int builtin_tagged(VM *vm) {
    const char *name = r_arg_str(vm, 0);
    r_popn(vm, vm->cur_argc);
    int out = vm_array_new(vm);
    if (out >= 0) {
        for (int t = 0; t < g_tag_n; t++) {
            if (strcmp(g_tag_name[t], name) == 0) {
                for (int i = 0; i < g_tag_count[t]; i++) {
                    Value v; v.type = VAL_STRING; v.ival = 1; v.fval = 0; v.sval = g_tag_items[t][i];
                    vm_array_push(vm, out, &v);
                }
                break;
            }
        }
    }
    Value arrv; arrv.type = VAL_ARRAY; arrv.ival = out + 1; arrv.fval = 0; arrv.sval = NULL;
    r_push(vm, arrv);
    return 1;
}
static int builtin_tag_count(VM *vm) {
    const char *name = r_arg_str(vm, 0);
    r_popn(vm, vm->cur_argc);
    int c = 0;
    for (int t = 0; t < g_tag_n; t++) if (strcmp(g_tag_name[t], name) == 0) { c = g_tag_count[t]; break; }
    r_push_int(vm, c);
    return 1;
}

/* autosave(seconds): enable periodic auto-save (0 = off) */
static int builtin_autosave(VM *vm) {
    double s = r_arg_num(vm, 0);
    r_popn(vm, vm->cur_argc);
    vm->record_autosave_interval = (s > 0) ? (unsigned long long)(s * 1000.0) : 0;
    r_push_int(vm, 1);
    return 1;
}

static int builtin_save(VM *vm) {
    const char *path = vm->cur_argc >0 ? r_arg_str(vm, vm->cur_argc -1) : (vm->record_save_path ? vm->record_save_path : "save.dat");
    r_popn(vm, vm->cur_argc);
    record_save_to_file(vm, path);
    r_push_int(vm,1);
    return 1;
}

static int builtin_load(VM *vm) {
    const char *path = vm->cur_argc >0 ? r_arg_str(vm, vm->cur_argc -1) : (vm->record_save_path ? vm->record_save_path : "save.dat");
    r_popn(vm, vm->cur_argc);
    /* load into already-declared record globals */
    FILE *f = fopen(path, "rb");
    if (!f) { r_push_int(vm,0); return 1; }
    fseek(f,0,SEEK_END); long len = ftell(f); fseek(f,0,SEEK_SET);
    if (len <=0 || len > (1<<20)) { fclose(f); r_push_int(vm,0); return 1; }
    char *buf = malloc((size_t)len +1);
    fread(buf,1,(size_t)len,f); buf[len] = '\0';
    fclose(f);
    int ok =0;
    Value d = json_parse_value_text(vm, buf, &ok);
    free(buf);
    int applied =0;
    if (ok && d.type == VAL_DICT) {
        ArrayObj *a = vm_pool_slot(vm, d.ival -1);
        if (a) {
            for (int i =0; i +1 < a->count; i +=2) {
                Value *k = &a->items[i];
                if (k->type != VAL_STRING || !k->sval) continue;
                int idx = record_find_idx(vm, k->sval);
                if (idx >=0 && idx < vm->globalCount) {
                    VM_LOCK(vm);
                    r_free_value(&vm->globals[idx].val);
                    r_copy_value(&vm->globals[idx].val, &a->items[i+1]);
                    if (idx < vm->record_meta_count) vm->record_meta[idx].dirty =0;
                    VM_UNLOCK(vm);
                    applied++;
                }
            }
        }
    }
    r_push_int(vm, applied);
    return 1;
}

static int builtin_record_dirty(VM *vm) {
    r_popn(vm, vm->cur_argc);
    int aidx = vm_array_new(vm);
    if (aidx <0) { r_push_nil(vm); return 1; }
    for (int i =0; i < vm->record_meta_count; i++) {
        if (vm->record_meta[i].dirty && vm->record_names && vm->record_names[i]) {
            Value v; v.type = VAL_STRING; v.ival =1; v.fval =0; v.sval = vm->record_names[i];
            vm_array_push(vm, aidx, &v);
        }
    }
    Value arrv; arrv.type = VAL_ARRAY; arrv.ival = aidx +1; arrv.fval =0; arrv.sval = NULL;
    r_push(vm, arrv);
    return 1;
}

static int builtin_record_mark_clean(VM *vm) {
    const char *name = vm->cur_argc >0 ? r_arg_str(vm, vm->cur_argc -1) : "";
    r_popn(vm, vm->cur_argc);
    int idx = record_find_idx(vm, name);
    if (idx >=0 && idx < vm->record_meta_count) vm->record_meta[idx].dirty =0;
    r_push_int(vm, idx >=0 ? 1 : 0);
    return 1;
}

static int builtin_record_value(VM *vm) {
    const char *name = vm->cur_argc >0 ? r_arg_str(vm, vm->cur_argc -1) : "";
    r_popn(vm, vm->cur_argc);
    int idx = record_find_idx(vm, name);
    if (idx >=0 && idx < vm->globalCount) { r_push(vm, vm->globals[idx].val); }
    else r_push_nil(vm);
    return 1;
}

static int builtin_record_set_value(VM *vm) {
    const char *name = vm->cur_argc >1 ? r_arg_str(vm, vm->cur_argc -1) : "";
    Value v = vm->cur_argc >0 ? r_arg(vm, vm->cur_argc -2) : r_arg(vm,0);
    r_popn(vm, vm->cur_argc);
    int idx = record_find_idx(vm, name);
    if (idx >=0 && idx < vm->globalCount) {
        VM_LOCK(vm);
        r_free_value(&vm->globals[idx].val);
        r_copy_value(&vm->globals[idx].val, &v);
        if (idx < vm->record_meta_count) {
            vm->record_meta[idx].dirty =0;
            vm->record_meta[idx].version++;
        }
        VM_UNLOCK(vm);
        r_push_int(vm,1);
    } else r_push_int(vm,0);
    return 1;
}

static int builtin_record_get(VM *vm) {
    const char *name = vm->cur_argc >0 ? r_arg_str(vm, vm->cur_argc -1) : "";
    r_popn(vm, vm->cur_argc);
    int idx = record_find_idx(vm, name);
    if (idx <0) { r_push_nil(vm); return 1; }
    int aidx = vm_array_new(vm);
    if (aidx <0) { r_push_nil(vm); return 1; }
    Value k1; k1.type = VAL_STRING; k1.ival =1; k1.fval =0; k1.sval = "store";
    Value v1; v1.type = VAL_INT; v1.ival = vm->record_meta[idx].store; v1.fval =0; v1.sval = NULL;
    vm_dict_set(vm, aidx, &k1, &v1);
    Value k2; k2.type = VAL_STRING; k2.ival =1; k2.fval =0; k2.sval = "scope";
    Value v2; v2.type = VAL_INT; v2.ival = vm->record_meta[idx].scope; v2.fval =0; v2.sval = NULL;
    vm_dict_set(vm, aidx, &k2, &v2);
    Value k3; k3.type = VAL_STRING; k3.ival =1; k3.fval =0; k3.sval = "merge";
    Value v3; v3.type = VAL_INT; v3.ival = vm->record_meta[idx].merge; v3.fval =0; v3.sval = NULL;
    vm_dict_set(vm, aidx, &k3, &v3);
    Value k4; k4.type = VAL_STRING; k4.ival =1; k4.fval =0; k4.sval = "version";
    Value v4; v4.type = VAL_INT; v4.ival = vm->record_meta[idx].version; v4.fval =0; v4.sval = NULL;
    vm_dict_set(vm, aidx, &k4, &v4);
    Value d; d.type = VAL_DICT; d.ival = aidx +1; d.fval =0; d.sval = NULL;
    r_push(vm, d);
    return 1;
}

static int builtin_record_snapshot(VM *vm) {
    r_popn(vm, vm->cur_argc);
    Value d;
    record_save_to_dict(vm, &d);
    r_push(vm, d);
    return 1;
}

void record_mod_register(VM *vm) {
    vm_register_builtin_full(vm, "save", builtin_save, 1|CAP_IO, 0);
    vm_register_builtin(vm, "gui_autosave", builtin_autosave);
    vm_register_builtin(vm, "tag_register", builtin_tag_register);
    vm_register_builtin(vm, "tagged", builtin_tagged);
    vm_register_builtin(vm, "tag_count", builtin_tag_count);
    vm_register_builtin_full(vm, "load", builtin_load, 1|CAP_IO, 0);
    vm_register_builtin(vm, "record_dirty", builtin_record_dirty);
    vm_register_builtin(vm, "record_mark_clean", builtin_record_mark_clean);
    vm_register_builtin(vm, "record_value", builtin_record_value);
    vm_register_builtin(vm, "record_set_value", builtin_record_set_value);
    vm_register_builtin(vm, "record_get", builtin_record_get);
    vm_register_builtin(vm, "record_snapshot", builtin_record_snapshot);
}
