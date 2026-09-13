#include "runtime.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <regex.h>
#include "../platform/platform.h"
#include "../platform/thread.h"

static int posix_core_len(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    int n = 0;
    if (v->type == VAL_ARRAY) n = vm_array_len(vm, v->ival - 1);
    else if (v->type == VAL_SET && v->ival >= 0 && v->ival < vm->setCount) {
        SetObj *s = &vm->sets[v->ival];
        if (s->kind == 0 && s->compCount == 0) n = s->iCount + s->count;
        else { int a = vm_set_to_array(vm, v->ival); if (a >= 0) n = vm_array_len(vm, a); }
    }
    else if (v->type == VAL_DICT && v->ival > 0 && v->ival - 1 < vm->arrayCount) {
        ArrayObj *a = vm_pool_slot(vm, v->ival - 1); n = a ? a->count / 2 : 0;
    }
    else if (v->type == VAL_STRING) n = (int)strlen(v->sval ? v->sval : "");
    else if (v->type == VAL_INT) n = (int)v->ival;
    else if (v->type == VAL_FLOAT) n = (int)v->fval;
    pop(vm); push_int(vm, n); return 1;
}

static int posix_core_size(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    int n = -1;
    if (v->type == VAL_SET && v->ival >= 0 && v->ival < vm->setCount) {
        SetObj *s = &vm->sets[v->ival];
        if (s->kind == 0 && s->compCount == 0) n = s->iCount + s->count;
        else if (s->kind == 2 && s->lo > -1e300 && s->hi < 1e300) {
            double step = (s->nameIdx >= 0 && s->nameIdx <= 3) ? 1.0 : pow(10.0, -((s->nameIdx - 4) / 2 + 1));
            double lo = s->loInc ? s->lo : s->lo + step;
            double hi = s->hiInc ? s->hi : s->hi - step;
            if (hi >= lo) n = (int)((hi - lo) / step) + 1;
            else n = 0;
        }
    } else if (v->type == VAL_ARRAY && v->ival > 0 && v->ival - 1 < vm->arrayCount) {
        ArrayObj *a = vm_pool_slot(vm, v->ival - 1); n = a ? a->count : 0;
    } else if (v->type == VAL_DICT && v->ival > 0 && v->ival - 1 < vm->arrayCount) {
        ArrayObj *a = vm_pool_slot(vm, v->ival - 1); n = a ? a->count / 2 : 0;
    } else if (v->type == VAL_STRING) n = (int)strlen(v->sval ? v->sval : "");
    else if (v->type == VAL_INT) n = (int)v->ival;
    else if (v->type == VAL_FLOAT) n = (int)v->fval;
    pop(vm); if (n < 0) push_nil(vm); else push_int(vm, n); return 1;
}

static int posix_core_str(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    char buf[1024];
    if (v->type == VAL_INT) snprintf(buf, sizeof buf, "%d", v->ival);
    else if (v->type == VAL_FLOAT) snprintf(buf, sizeof buf, "%.17g", v->fval);
    else if (v->type == VAL_BOOL) snprintf(buf, sizeof buf, "%s", v->ival ? "true" : "false");
    else if (v->type == VAL_STRING) { char *s = strdup(v->sval ? v->sval : ""); pop(vm); push_string(vm, s); free(s); return 1; }
    else vm_value_to_string(vm, v, buf, sizeof buf);
    pop(vm); push_string(vm, buf); return 1;
}

static int posix_core_bool(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    int truth = v->type == VAL_BOOL ? v->ival != 0 : v->type == VAL_INT ? v->ival != 0 :
                v->type == VAL_FLOAT ? v->fval != 0.0 : v->type == VAL_STRING ? (v->sval && *v->sval) : v->type != VAL_NIL;
    pop(vm); push_bool(vm, truth); return 1;
}

static int posix_core_int(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)]; int64_t n = 0;
    if (v->type == VAL_STRING) n = strtoll(v->sval ? v->sval : "0", NULL, 10);
    else if (v->type == VAL_FLOAT) n = (int64_t)v->fval;
    else n = v->ival;
    pop(vm); push_int(vm, n); return 1;
}

static int posix_core_float(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)]; double n = 0.0;
    if (v->type == VAL_STRING) n = strtod(v->sval ? v->sval : "0", NULL);
    else if (v->type == VAL_INT) n = (double)v->ival;
    else n = v->fval;
    pop(vm); push_float(vm, n); return 1;
}

static int posix_core_round(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value nv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value xv = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    if (xv.type != VAL_INT && xv.type != VAL_FLOAT) {
        pop(vm); pop(vm); push_nil(vm); return 1;
    }
    int digits = nv.type == VAL_INT ? (int)nv.ival : (nv.type == VAL_FLOAT ? (int)nv.fval : 0);
    if (digits > 12) digits = 12;
    if (digits < -12) digits = -12;
    double x = xv.type == VAL_INT ? (double)xv.ival : xv.fval;
    double scale = 1.0;
    int count = digits < 0 ? -digits : digits;
    for (int i = 0; i < count; i++) scale *= 10.0;
    double out = digits >= 0 ? round(x * scale) / scale : round(x / scale) * scale;
    pop(vm); pop(vm); push_float(vm, out); return 1;
}

static int posix_core_match(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value pv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value sv = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    const char *pattern = pv.type == VAL_STRING && pv.sval ? pv.sval : "";
    const char *subject = sv.type == VAL_STRING && sv.sval ? sv.sval : "";
    char translated[2048]; size_t j = 0; int in_class = 0;
    for (size_t i = 0; pattern[i] && j + 16 < sizeof translated; i++) {
        if (pattern[i] == '[') in_class = 1;
        if (pattern[i] == ']' && in_class) in_class = 0;
        if (pattern[i] == '\\' && pattern[i + 1]) {
            const char *rep = NULL;
            if (in_class) {
                switch (pattern[i + 1]) {
                    case 'd': rep = "[:digit:]"; break;
                    case 'w': rep = "[:alnum:]_"; break;
                    case 's': rep = "[:space:]"; break;
                    default: break;
                }
            } else switch (pattern[i + 1]) {
                case 'd': rep = "[[:digit:]]"; break;
                case 'D': rep = "[^[:digit:]]"; break;
                case 'w': rep = "[[:alnum:]_]"; break;
                case 'W': rep = "[^[:alnum:]_]"; break;
                case 's': rep = "[[:space:]]"; break;
                case 'S': rep = "[^[:space:]]"; break;
                default: break;
            }
            if (rep) { size_t n = strlen(rep); memcpy(translated + j, rep, n); j += n; i++; continue; }
        }
        translated[j++] = pattern[i];
    }
    translated[j] = '\0';
    regex_t re; int ok = 0;
    if (regcomp(&re, translated, REG_EXTENDED) == 0) {
        ok = regexec(&re, subject, 0, NULL, 0) == 0;
        regfree(&re);
    }
    pop(vm); pop(vm); push_bool(vm, ok != 0); return 1;
}

static int posix_core_args(VM *vm) {
    int aidx = vm_array_new(vm);
    if (aidx < 0) { push_nil(vm); return 1; }
    for (int i = 0; i < vm->argc; i++) {
        Value item = { VAL_STRING, 0, 0, vm->argv && vm->argv[i] ? vm->argv[i] : "" };
        vm_array_push(vm, aidx, &item);
    }
    Value out = { VAL_ARRAY, aidx + 1, 0, NULL };
    vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = out;
    return 1;
}

static int posix_core_type(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *name = NULL;
    switch (v->type) {
        case VAL_INT: name = "int"; break;
        case VAL_FLOAT: name = "float"; break;
        case VAL_STRING: name = "string"; break;
        case VAL_BOOL: name = "bool"; break;
        case VAL_ARRAY: name = "array"; break;
        case VAL_DICT: name = "dict"; break;
        case VAL_SET: name = "set"; break;
        case VAL_NIL: pop(vm); push_nil(vm); return 1;
        default: name = "unknown"; break;
    }
    pop(vm);
    push_string(vm, name);
    return 1;
}

/* Keep the POSIX runtime's .range behavior aligned with the host runtime.
 * The compiler passes both the value and the global index so a `be` binding
 * can expose its declared set instead of a broad inferred numeric range. */
static int posix_core_range(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value gi = vm_cur_stack(vm)[vm_cur_sp(vm)];
    int gidx = gi.type == VAL_INT ? (int)gi.ival : -1;
    pop(vm);
    pop(vm);

    if (gidx >= 0 && gidx < vm->be_bound_cap && vm->be_bound[gidx] > 0) {
        int bidx = vm->be_bound[gidx] - 1;
        if (bidx >= 0 && bidx < vm->setCount) {
            Value out = { VAL_SET, bidx, 0, NULL };
            vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
            vm_cur_stack(vm)[vm_cur_sp(vm)] = out;
            return 1;
        }
    }
    if (v.type == VAL_SET && v.ival >= 0 && v.ival < vm->setCount) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = v;
        return 1;
    }

    /* Fallback ranges match the host runtime's broad numeric domains. */
    if (v.type == VAL_INT || v.type == VAL_FLOAT) {
        int sidx = vm_set_new(vm);
        if (sidx < 0) { push_nil(vm); return 1; }
        SetObj *s = &vm->sets[sidx];
        s->kind = 2;
        s->nameIdx = v.type == VAL_INT ? 1 : 24;
        s->lo = v.type == VAL_INT ? -2147483648.0 : -1e308;
        s->hi = v.type == VAL_INT ? 2147483647.0 : 1e308;
        s->loInc = v.type == VAL_INT ? 1 : 0;
        s->hiInc = v.type == VAL_INT ? 1 : 0;
        Value out = { VAL_SET, sidx, 0, NULL };
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = out;
        return 1;
    }
    push_nil(vm);
    return 1;
}

static int posix_core_list(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    if (v.type == VAL_ARRAY) { pop(vm); vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = v; return 1; }
    int idx = (v.type == VAL_SET) ? vm_set_to_array(vm, v.ival) : -1;
    pop(vm);
    if (idx < 0) { push_nil(vm); return 1; }
    Value out = { VAL_ARRAY, idx + 1, 0, NULL };
    vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = out;
    return 1;
}

static int posix_core_sum(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)]; double total = 0; int ok = 1, all_int = 1;
    if (v.type == VAL_SET && v.ival >= 0 && v.ival < vm->setCount) {
        SetObj *s = &vm->sets[v.ival];
        if (s->kind == 0 && s->compCount == 0) {
            ok = 1;
            for (int i = 0; i < s->iCount; i++) total += (double)s->i64[i];
            for (int i = 0; i < s->count; i++) {
                if (s->items[i].type == VAL_STRING || s->items[i].type == VAL_BOOL) { ok = 0; break; }
                if (s->items[i].type == VAL_FLOAT) all_int = 0;
                total += val_as_double(&s->items[i]);
            }
        }
    } else if (v.type == VAL_ARRAY && v.ival > 0 && v.ival - 1 < vm->arrayCount) {
        ArrayObj *a = vm_pool_slot(vm, v.ival - 1);
        for (int i = 0; i < a->count; i++) { if (a->items[i].type != VAL_INT && a->items[i].type != VAL_FLOAT) { ok = 0; break; } if (a->items[i].type == VAL_FLOAT) all_int = 0; total += val_as_double(&a->items[i]); }
    } else ok = 0;
    pop(vm); if (!ok) { push_nil(vm); return 1; }
    if (all_int) push_int(vm, (int64_t)total); else push_float(vm, total); return 1;
}

static int posix_core_push(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value item = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value arr = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    if (arr.type == VAL_ARRAY && arr.ival > 0 && arr.ival - 1 < vm->arrayCount)
        vm_array_push(vm, arr.ival - 1, &item);
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 2); push_int(vm, 1); return 1;
}

static int posix_core_pop(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value arr = vm_cur_stack(vm)[vm_cur_sp(vm)];
    if (arr.type != VAL_ARRAY || arr.ival <= 0 || arr.ival - 1 >= vm->arrayCount) { pop(vm); push_nil(vm); return 1; }
    Value out = vm_array_pop(vm, arr.ival - 1); pop(vm);
    if (!vm_push_value(vm, &out)) value_free(&out);
    return 1;
}

static int posix_core_join(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value last = vm_cur_stack(vm)[vm_cur_sp(vm)], arr;
    const char *sep = "";
    if (last.type == VAL_STRING && vm_cur_sp(vm) >= 1) { sep = last.sval ? last.sval : ""; arr = vm_cur_stack(vm)[vm_cur_sp(vm) - 1]; vm_cur_set_sp(vm, vm_cur_sp(vm) - 2); }
    else { arr = last; pop(vm); }
    if (arr.type != VAL_ARRAY || arr.ival <= 0 || arr.ival - 1 >= vm->arrayCount) { push_string(vm, ""); return 1; }
    ArrayObj *a = vm_pool_slot(vm, arr.ival - 1); size_t cap = 1;
    for (int i = 0; i < a->count; i++) cap += 256 + strlen(sep);
    char *buf = (char *)calloc(cap, 1); if (!buf) { push_string(vm, ""); return 1; }
    for (int i = 0; i < a->count; i++) { char part[256]; if (i) strcat(buf, sep); vm_value_to_string(vm, &a->items[i], part, sizeof part); strcat(buf, part); }
    push_string(vm, buf); free(buf); return 1;
}

static int posix_core_split(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value sv = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value dv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    char *src = strdup(sv.type == VAL_STRING && sv.sval ? sv.sval : "");
    char *sep = strdup(dv.type == VAL_STRING && dv.sval ? dv.sval : "");
    pop(vm); pop(vm);
    int aidx = vm_array_new(vm);
    if (aidx < 0) { free(src); free(sep); push_nil(vm); return 1; }
    if (!sep[0]) {
        char *space = strdup(" ");
        free(sep);
        sep = space;
    }
    if (!sep) { free(src); push_nil(vm); return 1; }
    size_t sl = strlen(sep);
    const char *p = src;
    while (*p) {
        const char *q = strstr(p, sep);
        size_t n = q ? (size_t)(q - p) : strlen(p);
        char *part = malloc(n + 1);
        if (!part) break;
        memcpy(part, p, n); part[n] = '\0';
        Value item = { VAL_STRING, 0, 0, part };
        vm_array_push(vm, aidx, &item);
        free(part);
        if (!q) break;
        p = q + sl;
    }
    free(src); free(sep);
    Value out = { VAL_ARRAY, aidx + 1, 0, NULL };
    vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = out;
    return 1;
}

static int posix_core_chars(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    char *s = strdup(v.type == VAL_STRING && v.sval ? v.sval : "");
    pop(vm);
    int aidx = vm_array_new(vm);
    if (aidx < 0) { free(s); push_nil(vm); return 1; }
    for (size_t i = 0; s[i]; i++) {
        char ch[2] = { s[i], '\0' };
        Value item = { VAL_STRING, 0, 0, ch };
        vm_array_push(vm, aidx, &item);
    }
    free(s);
    Value out = { VAL_ARRAY, aidx + 1, 0, NULL };
    vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = out;
    return 1;
}

static int posix_core_ord(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    int n = v.type == VAL_STRING && v.sval && v.sval[0] ? (unsigned char)v.sval[0] : 0;
    pop(vm); push_int(vm, n); return 1;
}

static int posix_core_chr(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    char s[2] = { (char)(v.type == VAL_INT ? v.ival & 0xff : 0), '\0' };
    pop(vm); push_string(vm, s); return 1;
}

static int posix_core_keys(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    int didx = v.type == VAL_DICT ? v.ival - 1 : -1;
    pop(vm);
    int aidx = vm_array_new(vm);
    if (aidx < 0) { push_nil(vm); return 1; }
    if (didx >= 0 && didx < vm->arrayCount) {
        ArrayObj *a = vm_pool_slot(vm, didx);
        for (int i = 0; a && i + 1 < a->count; i += 2) vm_array_push(vm, aidx, &a->items[i]);
    }
    Value out = { VAL_ARRAY, aidx + 1, 0, NULL };
    vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = out;
    return 1;
}

static int posix_core_has(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value key = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value dict = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    int found = 0;
    if (dict.type == VAL_DICT && dict.ival > 0 && dict.ival - 1 < vm->arrayCount) {
        ArrayObj *a = vm_pool_slot(vm, dict.ival - 1);
        for (int i = 0; a && i + 1 < a->count; i += 2)
            if (val_eq(&a->items[i], &key)) { found = 1; break; }
    }
    pop(vm); pop(vm); push_bool(vm, found != 0); return 1;
}

static int posix_core_remove(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value key = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value obj = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    int removed = 0;
    if (obj.type == VAL_DICT && obj.ival > 0 && obj.ival - 1 < vm->arrayCount) {
        removed = vm_dict_remove(vm, obj.ival - 1, &key) ? 1 : 0;
    } else if (obj.type == VAL_ARRAY && key.type == VAL_INT && obj.ival > 0 && obj.ival - 1 < vm->arrayCount) {
        ArrayObj *a = vm_pool_slot(vm, obj.ival - 1);
        int idx = (int)key.ival;
        if (a && idx >= 0 && idx < a->count) {
            value_free(&a->items[idx]);
            for (int i = idx; i + 1 < a->count; i++) a->items[i] = a->items[i + 1];
            a->count--;
            a->items[a->count] = (Value){ VAL_NIL, 0, 0, NULL };
            removed = 1;
        }
    }
    pop(vm); pop(vm); push_bool(vm, removed != 0); return 1;
}

static int posix_core_substr(VM *vm) {
    if (vm_cur_sp(vm) < 2) return 0;
    Value sv = vm_cur_stack(vm)[vm_cur_sp(vm) - 2];
    int start = (int)vm_cur_stack(vm)[vm_cur_sp(vm) - 1].ival;
    int len = (int)vm_cur_stack(vm)[vm_cur_sp(vm)].ival;
    char *s = strdup(sv.type == VAL_STRING && sv.sval ? sv.sval : "");
    pop(vm); pop(vm); pop(vm);
    int sl = (int)strlen(s);
    if (start < 0) start = sl + start;
    if (start < 0) start = 0; if (start > sl) start = sl;
    if (len < 0) len = 0; if (start + len > sl) len = sl - start;
    char *out = malloc((size_t)len + 1);
    if (!out) { free(s); push_string(vm, ""); return 1; }
    memcpy(out, s + start, (size_t)len); out[len] = '\0';
    push_string(vm, out); free(out); free(s); return 1;
}

static int posix_core_replace(VM *vm) {
    if (vm_cur_sp(vm) < 2) return 0;
    Value nv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value ov = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value sv = vm_cur_stack(vm)[vm_cur_sp(vm) - 2];
    char *s = strdup(sv.type == VAL_STRING && sv.sval ? sv.sval : "");
    char *old = strdup(ov.type == VAL_STRING && ov.sval ? ov.sval : "");
    char *newv = strdup(nv.type == VAL_STRING && nv.sval ? nv.sval : "");
    pop(vm); pop(vm); pop(vm);
    size_t sl = strlen(s), ol = strlen(old), nl = strlen(newv), cap = sl + 1;
    if (!ol) { push_string(vm, s); free(s); free(old); free(newv); return 1; }
    char *out = malloc(cap + 1); size_t used = 0;
    for (size_t i = 0; i < sl;) {
        const char *piece = (i + ol <= sl && memcmp(s + i, old, ol) == 0) ? newv : NULL;
        size_t pn = piece ? nl : 1;
        if (used + pn + 1 > cap) { while (used + pn + 1 > cap) cap *= 2; out = realloc(out, cap + 1); }
        if (piece) { memcpy(out + used, piece, pn); i += ol; } else out[used++] = s[i++];
        if (piece) used += pn;
    }
    out[used] = '\0'; push_string(vm, out);
    free(out); free(s); free(old); free(newv); return 1;
}

static int posix_core_startswith(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value p = vm_cur_stack(vm)[vm_cur_sp(vm)], s = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    const char *ss = s.type == VAL_STRING && s.sval ? s.sval : "";
    const char *pp = p.type == VAL_STRING && p.sval ? p.sval : "";
    int ok = strncmp(ss, pp, strlen(pp)) == 0; pop(vm); pop(vm); push_bool(vm, ok); return 1;
}

static int posix_core_endswith(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value p = vm_cur_stack(vm)[vm_cur_sp(vm)], s = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    const char *ss = s.type == VAL_STRING && s.sval ? s.sval : "";
    const char *pp = p.type == VAL_STRING && p.sval ? p.sval : "";
    size_t sl = strlen(ss), pl = strlen(pp); int ok = pl <= sl && memcmp(ss + sl - pl, pp, pl) == 0;
    pop(vm); pop(vm); push_bool(vm, ok); return 1;
}

static int posix_core_trim(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *s = v.type == VAL_STRING && v.sval ? v.sval : "";
    while (*s && strchr(" \t\r\n", *s)) s++;
    size_t n = strlen(s); while (n && strchr(" \t\r\n", s[n - 1])) n--;
    char *out = malloc(n + 1); if (!out) { pop(vm); push_string(vm, ""); return 1; }
    memcpy(out, s, n); out[n] = '\0'; pop(vm); push_string(vm, out); free(out); return 1;
}

static int posix_core_case(VM *vm, int upper) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    char *s = strdup(v.type == VAL_STRING && v.sval ? v.sval : "");
    for (char *p = s; *p; p++) if (upper && *p >= 'a' && *p <= 'z') *p -= 'a' - 'A'; else if (!upper && *p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
    pop(vm); push_string(vm, s); free(s); return 1;
}
static int posix_core_upper(VM *vm) { return posix_core_case(vm, 1); }
static int posix_core_lower(VM *vm) { return posix_core_case(vm, 0); }

static int posix_core_index(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value sub = vm_cur_stack(vm)[vm_cur_sp(vm)], str = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    const char *s = str.type == VAL_STRING && str.sval ? str.sval : "";
    const char *p = sub.type == VAL_STRING && sub.sval ? sub.sval : "";
    const char *hit = strstr(s, p); int idx = hit ? (int)(hit - s) : -1;
    pop(vm); pop(vm); push_int(vm, idx); return 1;
}

static int posix_gc_auto(VM *vm) {
    if (vm_cur_sp(vm) >= 0) {
        Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
        int on = v->type == VAL_INT ? v->ival : (int)v->fval;
        vm->gc_enabled = on ? 1 : 0;
        if (on && vm->gc_threshold <= 0) vm->gc_threshold = 2.0 * 1024.0 * 1024.0;
        pop(vm);
    }
    push_int(vm, vm->gc_enabled);
    return 1;
}

static int posix_gc_now(VM *vm) {
    int running = 0;
    VmThread *me = vm_get_cur_thread();
    for (int i = 0; i < VM_MAX_THREADS; i++) {
        VmThread *tt = vm->threads[i];
        if (tt && tt != me && tt->running) running++;
    }
    if (vm->gc_enabled && running == 0) gc_collect(vm);
    else vm->gc_pending = 1;
    push_int(vm, 1);
    return 1;
}

static int posix_gc_stats(VM *vm) {
    int aidx = vm_array_new(vm);
    if (aidx < 0) {
        push_nil(vm);
        return 1;
    }
    static const char *keys[] = { "runs", "freed", "enabled", "threshold", "used" };
    int64_t values[] = {
        vm->gc_runs, vm->gc_freed, vm->gc_enabled,
        (int64_t)vm->gc_threshold, (int64_t)vm->used_mem
    };
    for (int i = 0; i < 5; i++) {
        Value k = { VAL_STRING, 1, 0, (char *)keys[i], NULL };
        Value v = { VAL_INT, values[i], 0, NULL, NULL };
        vm_dict_set(vm, aidx, &k, &v);
    }
    Value dict = { VAL_DICT, aidx + 1, 0, NULL, NULL };
    vm_push_value(vm, &dict);
    return 1;
}
#include "../platform/dir.h"
#include "../platform/http_client.h"
#include "../platform/serial.h"
#include "../platform/process.h"

static int posix_random(VM *vm) { if (vm_cur_sp(vm) < 0) return 0; int n = vm_cur_stack(vm)[vm_cur_sp(vm)].ival; pop(vm); push_int(vm, n > 0 ? rand() % n : 0); return 1; }
static int posix_sqrt(VM *vm) { if (vm_cur_sp(vm) < 0) return 0; Value v = vm_cur_stack(vm)[vm_cur_sp(vm)]; pop(vm); push_float(vm, sqrt(v.type == VAL_INT ? (double)v.ival : v.fval)); return 1; }
static int posix_time_ms(VM *vm) { push_int(vm, (int)(im_platform_now_ms() & 0x7fffffff)); return 1; }
static int posix_sleep(VM *vm) { if (vm_cur_sp(vm) < 0) return 0; int ms=vm_cur_stack(vm)[vm_cur_sp(vm)].ival; pop(vm); if(ms>0) im_platform_sleep_ms((unsigned)ms); push_int(vm, 1); return 1; }
static int posix_read_file(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; const char *p=v.sval?v.sval:""; FILE *f=fopen(p,"rb"); pop(vm); if(!f){push_string(vm,"");return 1;} fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); char *b=(char*)malloc((size_t)n+1); if(!b){fclose(f);push_string(vm,"");return 1;} fread(b,1,(size_t)n,f); fclose(f); b[n]=0; push_string(vm,b); free(b); return 1; }
static int posix_write_file(VM *vm) { if(vm_cur_sp(vm)<1)return 0; Value data=vm_cur_stack(vm)[vm_cur_sp(vm)], path=vm_cur_stack(vm)[vm_cur_sp(vm)-1]; FILE *f=fopen(path.sval?path.sval:"","wb"); int ok=0; if(f){fputs(data.sval?data.sval:"",f); fclose(f); ok=1;} vm_cur_set_sp(vm,vm_cur_sp(vm)-2); push_int(vm,ok); return 1; }
static int posix_input(VM *vm) { if(vm_cur_sp(vm)>=0) pop(vm); char b[1024]; if(fgets(b,sizeof b,stdin)){size_t n=strlen(b); if(n&&b[n-1]=='\n')b[n-1]=0; push_string(vm,b);} else push_string(vm,""); return 1; }
static int posix_env(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; char b[4096]; int r=im_platform_getenv(v.sval?v.sval:"",b,sizeof b); pop(vm); push_string(vm,r==0?b:""); return 1; }
static int posix_capability(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; int ok=im_platform_has_capability(v.sval?v.sval:""); pop(vm); push_bool(vm,ok); return 1; }
static int posix_mkdir(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; int ok=im_platform_mkdirs(v.sval?v.sval:"")==0; pop(vm); push_bool(vm,ok); return 1; }
static int posix_list_dir(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value v=vm_cur_stack(vm)[vm_cur_sp(vm)]; ImDir *d=im_dir_open(v.sval?v.sval:""); pop(vm); if(!d){push_string(vm,"");return 1;} char n[512], all[4096]; all[0]=0; int first=1, isdir=0; while(im_dir_next_ex(d,n,sizeof n,&isdir)>0){ if(!first) strncat(all,"\n",sizeof all-strlen(all)-1); strncat(all,n,sizeof all-strlen(all)-1); first=0; } im_dir_close(d); push_string(vm,all); return 1; }

static int posix_http_req(VM *vm, int post) {
    int need = post ? 1 : 0; if (vm_cur_sp(vm) < need) return 0;
    Value uv = vm_cur_stack(vm)[vm_cur_sp(vm) - need]; Value dv = post ? vm_cur_stack(vm)[vm_cur_sp(vm)] : uv;
    char out[65536] = {0}; int status = 0;
    int ok = im_http_request(post ? "POST" : "GET", uv.sval ? uv.sval : "", post ? (dv.sval ? dv.sval : "") : NULL, out, sizeof out, &status) == 0 && status >= 200 && status < 400;
    vm_cur_set_sp(vm, vm_cur_sp(vm) - (post ? 2 : 1)); push_string(vm, ok ? out : ""); return 1;
}
static int posix_http_get(VM *vm) { return posix_http_req(vm, 0); }
static int posix_http_post(VM *vm) { return posix_http_req(vm, 1); }
static int posix_exec(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *cmd = v.type == VAL_STRING && v.sval ? v.sval : "";
    char out[65536] = {0};
    (void)im_process_capture(cmd, out, sizeof out, 15000);
    pop(vm); push_string(vm, out); return 1;
}
/* Hardware/UI operations keep the same names on POSIX.  Until a host grants
 * the corresponding PAL capability they fail deterministically instead of
 * becoming unknown builtins (which makes portable scripts diagnosable). */
static int posix_unsupported(VM *vm) {
    int n = vm_cur_sp(vm) + 1; if (n > 0) vm_cur_set_sp(vm, -1); push_int(vm, -1); return 1;
}
static int posix_serial_open(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value bv = vm_cur_stack(vm)[vm_cur_sp(vm)], pv = vm_cur_stack(vm)[vm_cur_sp(vm)-1];
    const char *path = pv.sval ? pv.sval : ""; int baud = bv.type == VAL_INT ? bv.ival : 9600;
    int fd = im_serial_open(path, baud);
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 2);
    if (fd < 0) { push_int(vm, -1); return 1; }
    push_int(vm, fd); return 1;
}
static int posix_serial_write(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value hv=vm_cur_stack(vm)[vm_cur_sp(vm)-1], dv=vm_cur_stack(vm)[vm_cur_sp(vm)];
    int n = (hv.type == VAL_INT && dv.type == VAL_STRING && dv.sval) ? im_serial_write(hv.ival, dv.sval, strlen(dv.sval)) : -1;
    vm_cur_set_sp(vm, vm_cur_sp(vm)-2); push_int(vm, n < 0 ? -1 : n); return 1;
}
static int posix_serial_read(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value hv=vm_cur_stack(vm)[vm_cur_sp(vm)-1], nv=vm_cur_stack(vm)[vm_cur_sp(vm)];
    int cap = nv.type == VAL_INT ? nv.ival : 256; if (cap < 1) cap = 1; if (cap > 65536) cap = 65536;
    char *buf = (char*)malloc((size_t)cap + 1); int n = (hv.type == VAL_INT) ? im_serial_read(hv.ival, buf, (size_t)cap) : -1;
    if (n < 0) n = 0;
    buf[n] = 0; vm_cur_set_sp(vm, vm_cur_sp(vm)-2); push_string(vm, buf); free(buf); return 1;
}
static int posix_serial_close(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value hv=vm_cur_stack(vm)[vm_cur_sp(vm)]; int ok = hv.type == VAL_INT && im_serial_close(hv.ival) == 0; pop(vm); push_int(vm, ok ? 1 : 0); return 1;
}

static int posix_usage(VM *vm) {
    int aidx = vm_array_new(vm);
    if (aidx < 0) { push_nil(vm); return 1; }
    const char *keys[] = { "mem", "mem_limit", "threads", "threads_limit",
                           "time", "time_limit", "inst_limit" };
    double vals[] = { vm->used_mem, vm->limit_mem, (double)vm->active_threads,
                      (double)vm->limit_threads,
                      vm->t_start ? (double)(im_platform_now_ms() - vm->t_start) / 1000.0 : 0.0,
                      vm->limit_time, vm->limit_inst };
    for (int i = 0; i < 7; i++) {
        Value k = { VAL_STRING, 1, 0, (char *)keys[i], NULL };
        Value v = { VAL_FLOAT, 0, vals[i], NULL, NULL };
        vm_dict_set(vm, aidx, &k, &v);
    }
    Value out = { VAL_DICT, aidx + 1, 0, NULL, NULL };
    vm_push_value(vm, &out);
    return 1;
}

static int posix_mod_limit(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    double mem = 0, vram = 0, time = 0;
    if (argc > 0) {
        Value v = vm_cur_stack(vm)[argc - 1];
        time = v.type == VAL_INT ? (double)v.ival : v.type == VAL_FLOAT ? v.fval : 0;
    }
    if (argc > 1) {
        Value v = vm_cur_stack(vm)[argc - 2];
        vram = v.type == VAL_INT ? (double)v.ival : v.type == VAL_FLOAT ? v.fval : 0;
    }
    if (argc > 2) {
        Value v = vm_cur_stack(vm)[argc - 3];
        mem = v.type == VAL_INT ? (double)v.ival : v.type == VAL_FLOAT ? v.fval : 0;
    }
    vm_cur_set_sp(vm, -1);
    vm->limit_mem = mem > 0 ? mem * 1048576.0 : 0;
    vm->limit_vram = vram > 0 ? vram * 1048576.0 : 0;
    vm->limit_time = time;
    push_int(vm, 1);
    return 1;
}

static int posix_mod_usage(VM *vm) {
    int aidx = vm_array_new(vm);
    if (aidx < 0) { push_nil(vm); return 1; }
    Value k = { VAL_STRING, 1, 0, "mem", NULL };
    Value v = { VAL_FLOAT, 0, vm->used_mem, NULL, NULL };
    vm_dict_set(vm, aidx, &k, &v);
    k.sval = "time";
    v.fval = vm->t_start ? (double)(im_platform_now_ms() - vm->t_start) / 1000.0 : 0.0;
    vm_dict_set(vm, aidx, &k, &v);
    Value out = { VAL_DICT, aidx + 1, 0, NULL, NULL };
    vm_push_value(vm, &out);
    return 1;
}

static int posix_list_params(VM *vm) {
    int aidx = vm_array_new(vm);
    if (aidx < 0) { push_nil(vm); return 1; }
    for (int i = 0; i < vm->globalCount; i++) {
        const char *name = vm->globals[i].name;
        if (!name || strncmp(name, "u.", 2) == 0 || !strchr(name, '.')) continue;
        Value v = { VAL_STRING, 0, 0, (char *)name, NULL };
        vm_array_push(vm, aidx, &v);
    }
    Value out = { VAL_ARRAY, aidx + 1, 0, NULL, NULL };
    vm_push_value(vm, &out);
    return 1;
}

static int posix_atomic_find(VM *vm, const char *name, int create) {
    if (!name) return -1;
    for (int i = 0; i < vm->globalCount; i++)
        if (vm->globals[i].name && strcmp(vm->globals[i].name, name) == 0) return i;
    if (!create) return -1;
    VM_LOCK(vm);
    for (int i = 0; i < vm->globalCount; i++)
        if (vm->globals[i].name && strcmp(vm->globals[i].name, name) == 0) {
            VM_UNLOCK(vm); return i;
        }
    if (vm->globalCount >= vm->globalCap) vm_global_grow(vm, vm->globalCount);
    int idx = vm->globalCount++;
    vm->globals[idx].name = strdup(name);
    vm->globals[idx].val = (Value){ VAL_INT, 0, 0, NULL, NULL };
    VM_UNLOCK(vm);
    return idx;
}

static int posix_atomic_add(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value name = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value delta = vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *n = name.type == VAL_STRING ? name.sval : NULL;
    int d = delta.type == VAL_INT ? delta.ival : (int)delta.fval;
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 2);
    int idx = posix_atomic_find(vm, n, 1);
    if (idx < 0) { push_int(vm, 0); return 1; }
    int old = __sync_fetch_and_add(&vm->globals[idx].val.ival, d);
    vm->globals[idx].val.type = VAL_INT;
    push_int(vm, old + d);
    return 1;
}

static int posix_atomic_get(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value name = vm_cur_stack(vm)[vm_cur_sp(vm)];
    int idx = posix_atomic_find(vm, name.type == VAL_STRING ? name.sval : NULL, 0);
    pop(vm);
    push_int(vm, idx < 0 ? 0 : __sync_add_and_fetch(&vm->globals[idx].val.ival, 0));
    return 1;
}

static int posix_atomic_set(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value name = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value value = vm_cur_stack(vm)[vm_cur_sp(vm)];
    int val = value.type == VAL_INT ? value.ival : (int)value.fval;
    int idx = posix_atomic_find(vm, name.type == VAL_STRING ? name.sval : NULL, 1);
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 2);
    if (idx < 0) { push_int(vm, 0); return 1; }
    __sync_lock_test_and_set(&vm->globals[idx].val.ival, val);
    vm->globals[idx].val.type = VAL_INT;
    push_int(vm, val);
    return 1;
}

static double posix_entity_arg(VM *vm, int index) {
    Value v = vm_cur_stack(vm)[index];
    return v.type == VAL_INT ? (double)v.ival : v.type == VAL_FLOAT ? v.fval : 0;
}

static int posix_entity_spawn(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 3) return 0;
    double x = posix_entity_arg(vm, argc - 3), y = posix_entity_arg(vm, argc - 2);
    int kind = (int)posix_entity_arg(vm, argc - 1);
    vm_cur_set_sp(vm, -1);
    int id;
    if (vm->ent_free_head >= 0) {
        id = vm->ent_free_head;
        vm->ent_free_head = vm->ent_free[id];
    } else {
        if (vm->ent_count >= vm->ent_cap) {
            int nc = vm->ent_cap ? vm->ent_cap * 2 : 1024;
            vm->ent_x = realloc(vm->ent_x, (size_t)nc * sizeof(float));
            vm->ent_y = realloc(vm->ent_y, (size_t)nc * sizeof(float));
            vm->ent_vx = realloc(vm->ent_vx, (size_t)nc * sizeof(float));
            vm->ent_vy = realloc(vm->ent_vy, (size_t)nc * sizeof(float));
            vm->ent_hp = realloc(vm->ent_hp, (size_t)nc * sizeof(int));
            vm->ent_kind = realloc(vm->ent_kind, (size_t)nc * sizeof(int));
            vm->ent_free = realloc(vm->ent_free, (size_t)nc * sizeof(int));
            vm->ent_cap = nc;
        }
        id = vm->ent_count++;
    }
    vm->ent_x[id] = (float)x; vm->ent_y[id] = (float)y;
    vm->ent_vx[id] = vm->ent_vy[id] = 0;
    vm->ent_hp[id] = 1; vm->ent_kind[id] = kind;
    vm->ent_grid_dirty = 1;
    push_int(vm, id);
    return 1;
}

static int posix_entity_kill(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 1) return 0;
    int id = (int)posix_entity_arg(vm, argc - 1);
    vm_cur_set_sp(vm, -1);
    if (id < 0 || id >= vm->ent_count || vm->ent_hp[id] < 0) { push_int(vm, 0); return 1; }
    vm->ent_hp[id] = -1; vm->ent_free[id] = vm->ent_free_head;
    vm->ent_free_head = id; vm->ent_grid_dirty = 1;
    push_int(vm, 1);
    return 1;
}

static int posix_entity_count(VM *vm) {
    vm_cur_set_sp(vm, -1);
    push_int(vm, vm->ent_count);
    return 1;
}

static int posix_entity_clear(VM *vm) {
    vm_cur_set_sp(vm, -1);
    vm->ent_count = 0; vm->ent_free_head = -1; vm->ent_grid_dirty = 1;
    push_int(vm, 1);
    return 1;
}

static int posix_entity_set(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 3) return 0;
    int id = (int)posix_entity_arg(vm, argc - 3);
    Value key = vm_cur_stack(vm)[argc - 2];
    double value = posix_entity_arg(vm, argc - 1);
    vm_cur_set_sp(vm, -1);
    if (id < 0 || id >= vm->ent_count || vm->ent_hp[id] < 0 || key.type != VAL_STRING) { push_int(vm, 0); return 1; }
    if (!strcmp(key.sval, "x")) { vm->ent_x[id] = (float)value; vm->ent_grid_dirty = 1; }
    else if (!strcmp(key.sval, "y")) { vm->ent_y[id] = (float)value; vm->ent_grid_dirty = 1; }
    else if (!strcmp(key.sval, "vx")) vm->ent_vx[id] = (float)value;
    else if (!strcmp(key.sval, "vy")) vm->ent_vy[id] = (float)value;
    else if (!strcmp(key.sval, "hp")) vm->ent_hp[id] = (int)value;
    else if (!strcmp(key.sval, "kind")) vm->ent_kind[id] = (int)value;
    else { push_int(vm, 0); return 1; }
    push_int(vm, 1);
    return 1;
}

static int posix_entity_get(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 2) return 0;
    int id = (int)posix_entity_arg(vm, argc - 2);
    Value key = vm_cur_stack(vm)[argc - 1];
    vm_cur_set_sp(vm, -1);
    if (id < 0 || id >= vm->ent_count || vm->ent_hp[id] < 0 || key.type != VAL_STRING) { push_nil(vm); return 1; }
    if (!strcmp(key.sval, "x")) push_float(vm, vm->ent_x[id]);
    else if (!strcmp(key.sval, "y")) push_float(vm, vm->ent_y[id]);
    else if (!strcmp(key.sval, "vx")) push_float(vm, vm->ent_vx[id]);
    else if (!strcmp(key.sval, "vy")) push_float(vm, vm->ent_vy[id]);
    else if (!strcmp(key.sval, "hp")) push_int(vm, vm->ent_hp[id]);
    else if (!strcmp(key.sval, "kind")) push_int(vm, vm->ent_kind[id]);
    else push_nil(vm);
    return 1;
}

static int posix_entity_neighbors(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 3) return 0;
    double x = posix_entity_arg(vm, argc - 3), y = posix_entity_arg(vm, argc - 2);
    double r = posix_entity_arg(vm, argc - 1);
    vm_cur_set_sp(vm, -1);
    int aidx = vm_array_new(vm);
    if (aidx < 0) { push_nil(vm); return 1; }
    double rr = r * r;
    for (int i = 0; i < vm->ent_count; i++) {
        if (vm->ent_hp[i] < 0) continue;
        double dx = vm->ent_x[i] - x, dy = vm->ent_y[i] - y;
        if (dx * dx + dy * dy <= rr) {
            Value id = { VAL_INT, i, 0, NULL, NULL };
            vm_array_push(vm, aidx, &id);
        }
    }
    Value out = { VAL_ARRAY, aidx + 1, 0, NULL, NULL };
    vm_push_value(vm, &out);
    return 1;
}

static int posix_entity_at(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 2) return 0;
    double x = posix_entity_arg(vm, argc - 2), y = posix_entity_arg(vm, argc - 1);
    vm_cur_set_sp(vm, -1);
    for (int i = 0; i < vm->ent_count; i++) {
        if (vm->ent_hp[i] >= 0 && vm->ent_x[i] == (float)x && vm->ent_y[i] == (float)y) {
            push_int(vm, i); return 1;
        }
    }
    push_int(vm, -1);
    return 1;
}

static int posix_spi_meta(VM *vm) {
    if (vm_cur_sp(vm) < 2) return 0;
    Value id = vm_cur_stack(vm)[vm_cur_sp(vm) - 2];
    Value version = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value caps = vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *name = id.type == VAL_STRING ? id.sval : "anon";
    int mask = caps.type == VAL_INT ? caps.ival : 0;
    if (caps.type == VAL_STRING && caps.sval) {
        if (strstr(caps.sval, "io")) mask |= CAP_IO;
        if (strstr(caps.sval, "net")) mask |= CAP_NET;
        if (strstr(caps.sval, "ai")) mask |= CAP_AI;
        if (strstr(caps.sval, "verse")) mask |= CAP_VERSE;
        if (strstr(caps.sval, "dbg")) mask |= CAP_DBG;
        if (strstr(caps.sval, "proc")) mask |= CAP_PROC;
        if (strstr(caps.sval, "all")) mask |= CAP_MASK;
    }
    vm->mod_caps = mask;
    int found = -1;
    for (int i = 0; i < vm->modCount; i++) if (!strcmp(vm->mods[i].id, name)) found = i;
    if (found < 0 && vm->modCount < 32) found = vm->modCount++;
    if (found >= 0) {
        snprintf(vm->mods[found].id, sizeof vm->mods[found].id, "%s", name);
        vm->mods[found].version = version.type == VAL_INT ? version.ival : (int)version.fval;
        vm->mods[found].caps = mask;
    }
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 3);
    return 1;
}

static int posix_spi_on(VM *vm) {
    if (vm_cur_sp(vm) < 1) { push_int(vm, 0); return 1; }
    Value event = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value function = vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *event_name = event.type == VAL_STRING ? event.sval : "";
    const char *function_name = function.type == VAL_STRING ? function.sval : "";
    int tidx = -1;
    if (vm->code) {
        for (int i = 0; i < vm->code->thread_count; i++) {
            if (vm->code->thread_names[i] && !strcmp(vm->code->thread_names[i], function_name)) {
                tidx = i;
                break;
            }
        }
    }
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 2);
    if (tidx < 0) { push_int(vm, 0); return 1; }
    VM_LOCK(vm);
    if (vm->spi_sub_count >= vm->spi_sub_cap) {
        int cap = vm->spi_sub_cap ? vm->spi_sub_cap * 2 : 8;
        SpiSub *subs = realloc(vm->spi_subs, (size_t)cap * sizeof(*subs));
        if (subs) { vm->spi_subs = subs; vm->spi_sub_cap = cap; }
    }
    int ok = 0;
    if (vm->spi_sub_count < vm->spi_sub_cap) {
        vm->spi_subs[vm->spi_sub_count].event = strdup(event_name ? event_name : "");
        vm->spi_subs[vm->spi_sub_count].tidx = tidx;
        vm->spi_sub_count++;
        ok = 1;
    }
    VM_UNLOCK(vm);
    push_int(vm, ok);
    return 1;
}

static int posix_spi_emit(VM *vm) {
    if (vm_cur_sp(vm) < 1) { push_int(vm, 0); return 1; }
    Value event = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value data = vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *event_name = event.type == VAL_STRING ? event.sval : "";
    Value published = data;
    if (data.type == VAL_STRING) {
        published.type = VAL_STRING;
        published.ival = 1;
        published.sval = (char *)vm_intern(vm, data.sval ? data.sval : "");
    }
    for (int i = 0; i < vm->globalCount; i++) {
        if (vm->globals[i].name && !strcmp(vm->globals[i].name, "__spi_data")) {
            vm_value_assign(&vm->globals[i].val, &published);
            break;
        }
    }
    VmThread *current = vm_get_cur_thread();
    int fired = 0;
    for (int i = 0; i < vm->spi_sub_count; i++) {
        if (strcmp(vm->spi_subs[i].event, event_name ? event_name : "")) continue;
        VmThread *thread = vm_os_thread_start(vm, vm->code, vm->spi_subs[i].tidx, current, 0);
        if (!thread) continue;
        if (thread->os_handle) {
            if (im_thread_join(thread->os_handle, 3000) == 0) {
                im_thread_close(thread->os_handle);
                thread->os_handle = NULL;
            }
        }
        fired++;
    }
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 2);
    push_int(vm, fired);
    return 1;
}

static int posix_spi_caps(VM *vm) { push_int(vm, vm->mod_caps); return 1; }

static int posix_spi_has(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    int bi = v.type == VAL_STRING ? builtin_lookup(vm, v.sval ? v.sval : "") : -1;
    int ok = 0;
    if (bi >= 0 && !(vm->safe_mode && (vm->builtins[bi].flags & 1)) &&
        (vm->mod_caps < 0 || !(vm->builtins[bi].flags & CAP_MASK) ||
         (vm->mod_caps & vm->builtins[bi].flags))) ok = 1;
    pop(vm); push_int(vm, ok); return 1;
}

static int posix_spi_mods(VM *vm) {
    int aidx = vm_array_new(vm);
    if (aidx < 0) { push_nil(vm); return 1; }
    for (int i = 0; i < vm->modCount; i++) {
        int didx = vm_array_new(vm);
        if (didx < 0) continue;
        Value k = { VAL_STRING, 1, 0, "id", NULL };
        Value v = { VAL_STRING, 1, 0, vm->mods[i].id, NULL };
        vm_dict_set(vm, didx, &k, &v);
        k.sval = "version"; v.type = VAL_INT; v.ival = vm->mods[i].version; v.sval = NULL;
        vm_dict_set(vm, didx, &k, &v);
        k.sval = "caps"; v.ival = vm->mods[i].caps;
        vm_dict_set(vm, didx, &k, &v);
        Value d = { VAL_DICT, didx + 1, 0, NULL, NULL };
        vm_array_push(vm, aidx, &d);
    }
    Value out = { VAL_ARRAY, aidx + 1, 0, NULL, NULL };
    vm_push_value(vm, &out);
    return 1;
}

/* POSIX baseline keeps the portable runtime surface available. APIs that
 * require the host compiler or a platform-specific persistence format fail
 * explicitly until their backend is shared with this runtime. */
void runtime_register_builtins(VM *vm) {
    vm_register_builtin(vm, "random", posix_random);
    vm_register_builtin(vm, "sqrt", posix_sqrt);
    vm_register_builtin(vm, "time_ms", posix_time_ms);
    vm_register_builtin(vm, "sleep_ms", posix_sleep);
    vm_register_builtin(vm, "read_file", posix_read_file);
    vm_register_builtin(vm, "write_file", posix_write_file);
    vm_register_builtin(vm, "input", posix_input);
    vm_register_builtin(vm, "env", posix_env);
    vm_register_builtin(vm, "mkdir", posix_mkdir);
    vm_register_builtin(vm, "list_dir", posix_list_dir);
    vm_register_builtin(vm, "http_get", posix_http_get);
    vm_register_builtin(vm, "http_post", posix_http_post);
    vm_register_builtin_full(vm, "exec", posix_exec, 1 | CAP_PROC, 0);
    vm_register_builtin(vm, "serial_open", posix_serial_open);
    vm_register_builtin(vm, "serial_write", posix_serial_write);
    vm_register_builtin(vm, "serial_read", posix_serial_read);
    vm_register_builtin(vm, "serial_close", posix_serial_close);
    vm_register_builtin(vm, "key_press", posix_unsupported);
    vm_register_builtin(vm, "mouse_move", posix_unsupported);
    vm_register_builtin(vm, "mouse_click", posix_unsupported);
    vm_register_builtin(vm, "has_capability", posix_capability);
    vm_register_builtin(vm, "len", posix_core_len);
    vm_register_builtin(vm, "size", posix_core_size);
    vm_register_builtin(vm, "str", posix_core_str);
    vm_register_builtin(vm, "bool", posix_core_bool);
    vm_register_builtin(vm, "int", posix_core_int);
    vm_register_builtin(vm, "float", posix_core_float);
    vm_register_builtin(vm, "round", posix_core_round);
    vm_register_builtin(vm, "match", posix_core_match);
    vm_register_builtin(vm, "args", posix_core_args);
    vm_register_builtin(vm, "type", posix_core_type);
    vm_register_builtin(vm, "range", posix_core_range);
    vm_register_builtin(vm, "list", posix_core_list);
    vm_register_builtin(vm, "sum", posix_core_sum);
    vm_register_builtin(vm, "push", posix_core_push);
    vm_register_builtin(vm, "pop", posix_core_pop);
    vm_register_builtin(vm, "join", posix_core_join);
    vm_register_builtin(vm, "split", posix_core_split);
    vm_register_builtin(vm, "chars", posix_core_chars);
    vm_register_builtin(vm, "ord", posix_core_ord);
    vm_register_builtin(vm, "chr", posix_core_chr);
    vm_register_builtin(vm, "keys", posix_core_keys);
    vm_register_builtin(vm, "has", posix_core_has);
    vm_register_builtin(vm, "remove", posix_core_remove);
    vm_register_builtin(vm, "substr", posix_core_substr);
    vm_register_builtin(vm, "replace", posix_core_replace);
    vm_register_builtin(vm, "startswith", posix_core_startswith);
    vm_register_builtin(vm, "endswith", posix_core_endswith);
    vm_register_builtin(vm, "trim", posix_core_trim);
    vm_register_builtin(vm, "upper", posix_core_upper);
    vm_register_builtin(vm, "lower", posix_core_lower);
    vm_register_builtin(vm, "index", posix_core_index);
    vm_register_builtin(vm, "gc_auto", posix_gc_auto);
    vm_register_builtin(vm, "gc_now", posix_gc_now);
    vm_register_builtin(vm, "gc_stats", posix_gc_stats);
    vm_register_builtin_full(vm, "vm_exec", posix_unsupported, 1 | CAP_DBG | CAP_PROC, 0);
    vm_register_builtin(vm, "usage", posix_usage);
    vm_register_builtin_full(vm, "load_params", posix_unsupported, 1 | CAP_IO, 0);
    vm_register_builtin_full(vm, "save_params", posix_unsupported, 1 | CAP_IO, 0);
    vm_register_builtin(vm, "list_params", posix_list_params);
    vm_register_builtin(vm, "spi_meta", posix_spi_meta);
    vm_register_builtin(vm, "spi_on", posix_spi_on);
    vm_register_builtin(vm, "spi_emit", posix_spi_emit);
    vm_register_builtin(vm, "spi_caps", posix_spi_caps);
    vm_register_builtin(vm, "spi_has", posix_spi_has);
    vm_register_builtin(vm, "spi_mods", posix_spi_mods);
    vm_register_builtin(vm, "mod_limit", posix_mod_limit);
    vm_register_builtin(vm, "mod_usage", posix_mod_usage);
    vm_register_builtin(vm, "atomic_add", posix_atomic_add);
    vm_register_builtin(vm, "atomic_get", posix_atomic_get);
    vm_register_builtin(vm, "atomic_set", posix_atomic_set);
    vm_register_builtin(vm, "entity_spawn", posix_entity_spawn);
    vm_register_builtin(vm, "entity_kill", posix_entity_kill);
    vm_register_builtin(vm, "entity_set", posix_entity_set);
    vm_register_builtin(vm, "entity_get", posix_entity_get);
    vm_register_builtin(vm, "entity_count", posix_entity_count);
    vm_register_builtin(vm, "entity_clear", posix_entity_clear);
    vm_register_builtin(vm, "entity_neighbors", posix_entity_neighbors);
    vm_register_builtin(vm, "entity_at", posix_entity_at);
}

void record_load_from_file(VM *vm, const char *path) { (void)vm; (void)path; }
void record_save_to_file(VM *vm, const char *path) { (void)vm; (void)path; }
