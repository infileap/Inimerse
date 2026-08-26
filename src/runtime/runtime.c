#include "runtime.h"
#include "bytecode.h"
#include "../platform/platform.h"
#include "../platform/sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

static int builtin_random(VM *vm) { if (vm_cur_sp(vm)<0) return 0; int max=vm_cur_stack(vm)[vm_cur_sp(vm)].ival; vm_cur_set_sp(vm, vm_cur_sp(vm) - 1); push_int(vm, rand()%max); return 1; }
static int builtin_sqrt(VM *vm) { if (vm_cur_sp(vm)<0) return 0; double val=(vm_cur_stack(vm)[vm_cur_sp(vm)].type==VAL_INT)?vm_cur_stack(vm)[vm_cur_sp(vm)].ival:vm_cur_stack(vm)[vm_cur_sp(vm)].fval; vm_cur_set_sp(vm, vm_cur_sp(vm) - 1); push_float(vm, sqrt(val)); return 1; }
static int builtin_read_file(VM *vm) { if (vm_cur_sp(vm)<0) return 0; Value _pv=vm_cur_stack(vm)[vm_cur_sp(vm)]; char *fn=strdup(_pv.sval ? _pv.sval : ""); vm_cur_set_sp(vm, vm_cur_sp(vm) - 1); if (_pv.type==VAL_STRING && _pv.ival!=1) free(_pv.sval); FILE *f=fopen(fn,"rb"); free(fn); if(!f){push_string(vm,"");return 1;} fseek(f,0,SEEK_END); long len=ftell(f); fseek(f,0,SEEK_SET); char *buf=malloc(len+1); fread(buf,1,len,f); buf[len]='\0'; fclose(f); push_string(vm,buf); free(buf); return 1; }
static int builtin_write_file(VM *vm) { if (vm_cur_sp(vm)<1) return 0; Value _pw=vm_cur_stack(vm)[vm_cur_sp(vm)]; Value _px=vm_cur_stack(vm)[vm_cur_sp(vm)-1]; char *content=strdup(_pw.sval ? _pw.sval : ""); char *fn=strdup(_px.sval ? _px.sval : ""); FILE *f=fopen(fn,"w"); int success=0; if(f){fputs(content,f);fclose(f);success=1;} vm_cur_set_sp(vm, vm_cur_sp(vm) - 2); if (_pw.type==VAL_STRING && _pw.ival!=1) free(_pw.sval); if (_px.type==VAL_STRING && _px.ival!=1) free(_px.sval); free(content); free(fn); push_int(vm,success); return 1; }
static int builtin_input(VM *vm) {
    char *prompt = strdup("");
    if (vm_cur_sp(vm)>=0 && vm_cur_stack(vm)[vm_cur_sp(vm)].type==VAL_STRING) { free(prompt); prompt = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : ""); pop(vm); }
#ifdef _WIN32
    UINT old=GetConsoleCP();
    SetConsoleCP(65001);
#endif
    printf("%s",prompt); fflush(stdout);
    char buf[1024];
    if(fgets(buf,sizeof(buf),stdin)){
        size_t len=strlen(buf);
        if(len>0&&buf[len-1]=='\n') buf[len-1]='\0';
        push_string(vm,buf);
    } else push_string(vm,"");
#ifdef _WIN32
    SetConsoleCP(old);
#endif
    free(prompt);
    return 1;
}
static int builtin_int(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value *v=&vm_cur_stack(vm)[vm_cur_sp(vm)]; int res=0; if(v->type==VAL_STRING) { const char *sv = v->sval?v->sval:""; int hx = (sv[0]=='0' && (sv[1]=='x'||sv[1]=='X')); res=(int)strtoll(sv, NULL, hx?16:10); } else if(v->type==VAL_FLOAT) res=(int)v->fval; else if(v->type==VAL_BOOL) res=v->ival?1:0; else if(v->type==VAL_INT) res=v->ival; pop(vm); push_int(vm,res); return 1; }
static int builtin_round(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value *nv = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value *xv = &vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    if (xv->type != VAL_INT && xv->type != VAL_FLOAT) {
        vm_throw_msg(vm, "round: expected number");
        return 1;
    }
    double x = (xv->type == VAL_INT) ? (double)xv->ival : xv->fval;
    int n = (int)((nv->type == VAL_INT) ? nv->ival : (nv->type == VAL_FLOAT ? nv->fval : 0.0));
    if (n > 12) n = 12;
    if (n < -12) n = -12;
    double r;
    if (n >= 0) {
        double scale = 1.0;
        for (int i = 0; i < n; i++) scale *= 10.0;
        r = round(x * scale) / scale;
    } else {
        double scale = 1.0;
        for (int i = 0; i < -n; i++) scale *= 10.0;
        r = round(x / scale) * scale;
    }
    pop(vm); pop(vm);
    push_float(vm, r);
    return 1;
}
static int builtin_float(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value *v=&vm_cur_stack(vm)[vm_cur_sp(vm)]; double res=0.0; if(v->type==VAL_STRING) res=atof(v->sval?v->sval:""); else if(v->type==VAL_INT) res=(double)v->ival; else if(v->type==VAL_FLOAT) res=v->fval; else if(v->type==VAL_BOOL) res=v->ival?1.0:0.0; pop(vm); push_float(vm,res); return 1; }
static int builtin_str(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value *v=&vm_cur_stack(vm)[vm_cur_sp(vm)]; char buf[1024]={0}; if(v->type==VAL_INT) snprintf(buf,sizeof(buf),"%d",v->ival); else if(v->type==VAL_FLOAT) snprintf(buf,sizeof(buf),"%g",v->fval); else if(v->type==VAL_BOOL) snprintf(buf,sizeof(buf),"%s",v->ival?"true":"false"); else if(v->type==VAL_STRING) { char *sv=strdup(v->sval?v->sval:""); pop(vm); push_string(vm,sv); free(sv); return 1; } else vm_value_to_string(vm, v, buf, sizeof(buf)); pop(vm); push_string(vm,buf); return 1; }
static int builtin_bool(VM *vm) { if(vm_cur_sp(vm)<0)return 0; Value *v=&vm_cur_stack(vm)[vm_cur_sp(vm)]; bool res=false; if(v->type==VAL_INT) res=v->ival!=0; else if(v->type==VAL_FLOAT) res=v->fval!=0.0; else if(v->type==VAL_STRING) res=v->sval&&strlen(v->sval)>0; else if(v->type==VAL_BOOL) res=v->ival!=0; pop(vm); push_bool(vm,res); return 1; }

/* ---------- 数组内置函数 ---------- */
static int builtin_len(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    int n = 0;
    if (v->type == VAL_ARRAY) n = vm_array_len(vm, v->ival - 1);
    else if (v->type == VAL_DICT) {
        int a = v->ival - 1;
        if (a >= 0 && a < vm->arrayCount) n = vm_pool_slot(vm, a)->count / 2;
    }
    else if (v->type == VAL_STRING) n = (int)strlen(v->sval ? v->sval : "");
    else if (v->type == VAL_INT) n = v->ival;
    else if (v->type == VAL_FLOAT) n = (int)v->fval;
    pop(vm);
    push_int(vm, n);
    return 1;
}

static int builtin_size(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    int n = -1;
    if (v->type == VAL_SET) {
        if (v->ival >= 0 && v->ival < vm->setCount) {
            SetObj *s = &vm->sets[v->ival];
            if (s->kind == 0 && s->compCount == 0) n = s->iCount + s->count;
            else if (s->kind == 2 && s->lo > -1e300 && s->hi < 1e300) {
                double step = (s->nameIdx >= 0 && s->nameIdx <= 3) ? 1.0 : pow(10.0, -((s->nameIdx - 4) / 2 + 1));
                if (step <= 0) step = 1.0;
                double lo = s->loInc ? s->lo : s->lo + step;
                double hi = s->hiInc ? s->hi : s->hi - step;
                if (hi < lo) n = 0; else n = (int)((hi - lo) / step) + 1;
            }
        }
    }
    else if (v->type == VAL_ARRAY) n = vm_array_len(vm, v->ival - 1);
    else if (v->type == VAL_DICT) { int a = v->ival - 1; if (a >= 0 && a < vm->arrayCount) n = vm_pool_slot(vm, a)->count / 2; }
    else if (v->type == VAL_STRING) n = (int)strlen(v->sval ? v->sval : "");
    else if (v->type == VAL_INT) n = v->ival;
    else if (v->type == VAL_FLOAT) n = (int)v->fval;
    pop(vm);
    if (n < 0) push_nil(vm); else push_int(vm, n);
    return 1;
}

static int builtin_list(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    int r = -1;
    if (v->type == VAL_SET && v->ival >= 0 && v->ival < vm->setCount) {
        r = vm_set_to_array(vm, v->ival);
    } else {
        pop(vm);
        vm_throw_msg(vm, "list: expected set");
        return 1;
    }
    pop(vm);
    if (r < 0) push_nil(vm);
    else { push_nil(vm); vm_cur_stack(vm)[vm_cur_sp(vm)].type = VAL_ARRAY; vm_cur_stack(vm)[vm_cur_sp(vm)].ival = r; }
    return 1;
}

static int builtin_sum(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    double sum = 0; int allInt = 1, ok = 0;
    if (v->type == VAL_SET && v->ival >= 0 && v->ival < vm->setCount) {
        SetObj *s = &vm->sets[v->ival];
        if (s->kind == 0 && s->compCount == 0) {
            ok = 1;
            for (int i = 0; i < s->iCount; i++) sum += (double)s->i64[i];
            for (int i = 0; i < s->count; i++) {
                if (s->items[i].type == VAL_STRING || s->items[i].type == VAL_BOOL) { ok = 0; break; }
                if (s->items[i].type == VAL_FLOAT) allInt = 0;
                sum += val_as_double(&s->items[i]);
            }
        }
    }
    else if (v->type == VAL_ARRAY) {
        int a = v->ival - 1;
        if (a >= 0 && a < vm->arrayCount) {
            ArrayObj *arr = vm_pool_slot(vm, a);
            ok = 1;
            for (int i = 0; i < arr->count; i++) {
                if (arr->items[i].type == VAL_STRING || arr->items[i].type == VAL_BOOL) { ok = 0; break; }
                if (arr->items[i].type == VAL_FLOAT) allInt = 0;
                sum += val_as_double(&arr->items[i]);
            }
        }
    }
    pop(vm);
    if (!ok) {
        if (v->type == VAL_SET || v->type == VAL_ARRAY) {
            vm_throw_msg(vm, "sum: non-numeric element");
            return 1;
        }
        push_nil(vm);
    }
    else if (allInt) push_int(vm, (int)sum);
    else push_float(vm, sum);
    return 1;
}

static int builtin_push(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value *item = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value *arrv = &vm_cur_stack(vm)[vm_cur_sp(vm)-1];
    if (arrv->type == VAL_ARRAY) {
        int a = arrv->ival - 1;
        if (a >= 0 && a < vm->arrayCount) vm_array_push(vm, a, item);
    }
    pop(vm); pop(vm);
    push_int(vm, 1);
    return 1;
}

static int builtin_pop(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *arrv = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    if (arrv->type == VAL_ARRAY) {
        int a = arrv->ival - 1;
        if (a >= 0 && a < vm->arrayCount) {
            Value popped = vm_array_pop(vm, a);
            pop(vm);
            if (popped.type == VAL_STRING) {
                push_string(vm, popped.sval);
                if (popped.ival != 1) free(popped.sval);
            }
            else if (popped.type == VAL_INT) push_int(vm, popped.ival);
            else if (popped.type == VAL_FLOAT) push_float(vm, popped.fval);
            else if (popped.type == VAL_BOOL) push_bool(vm, popped.ival != 0);
            else if (popped.type == VAL_ARRAY) { push_nil(vm); vm_cur_stack(vm)[vm_cur_sp(vm)] = popped; popped.sval = NULL; }
            else push_nil(vm);
            return 1;
        }
    }
    pop(vm);
    push_nil(vm);
    return 1;
}

/* join(arr) / join(arr, sep)：数组元素连接为字符??*/
static int builtin_join(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *last = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    char *sep = NULL;
    Value *arrv = NULL;
    if (last->type == VAL_STRING && vm_cur_sp(vm) >= 1 && vm_cur_stack(vm)[vm_cur_sp(vm)-1].type == VAL_ARRAY) {
        sep = strdup(last->sval ? last->sval : "");
        arrv = &vm_cur_stack(vm)[vm_cur_sp(vm)-1];
        pop(vm); pop(vm);
    } else if (last->type == VAL_ARRAY) {
        sep = strdup("");
        arrv = last;
        pop(vm);
    } else {
        pop(vm);
        push_string(vm, "");
        return 1;
    }
    int a = arrv->ival - 1;
    int n = (a >= 0 && a < vm->arrayCount) ? vm_pool_slot(vm, a)->count : 0;
    size_t total = 1;
    for (int i = 0; i < n; i++) {
        char tmp[256];
        vm_value_to_string(vm, &vm_pool_slot(vm, a)->items[i], tmp, sizeof(tmp));
        total += strlen(tmp) + strlen(sep) + 1;
    }
    char *buf = malloc(total);
    buf[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (i > 0) strcat(buf, sep);
        char tmp[256];
        vm_value_to_string(vm, &vm_pool_slot(vm, a)->items[i], tmp, sizeof(tmp));
        strcat(buf, tmp);
    }
    push_string(vm, buf);
    free(buf);
    free(sep);
    return 1;
}

/* split(str, sep)：按分隔符分割字符串为数??*/
static int builtin_split(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    /* 先拷贝参数（pop 会释放栈上的字符串），避免悬垂指??*/
    char *sep = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : "");
    char *str = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)-1].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)-1].sval : "");
    pop(vm); pop(vm);

    int aidx = vm_array_new(vm);
    if (aidx < 0) { free(sep); free(str); push_nil(vm); return 1; }

    if (!*sep) sep[0] = ' ';
    size_t slen = strlen(sep);
    const char *p = str;
    while (*p) {
        const char *q = strstr(p, sep);
        size_t len = q ? (size_t)(q - p) : strlen(p);
        char *part = malloc(len + 1);
        memcpy(part, p, len);
        part[len] = '\0';
        Value v; v.type = VAL_STRING; v.ival = 0; v.fval = 0; v.sval = part;
        vm_array_push(vm, aidx, &v);
        free(part);
        if (!q) break;
        p = q + slen;
    }
    free(sep);
    free(str);

    Value arrv; arrv.type = VAL_ARRAY; arrv.ival = aidx + 1; arrv.fval = 0; arrv.sval = NULL;
    if (vm_cur_sp(vm) < 1023) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = arrv;
    }
    return 1;
}

/* ---------- 字符??字典内置函数（自举基础能力??---------- */

/* chars(s)：字符串按字节拆成单字符数组 */
static int builtin_chars(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    char *s = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : "");
    pop(vm);
    int aidx = vm_array_new(vm);
    if (aidx < 0) { free(s); push_nil(vm); return 1; }
    for (const char *p = s; *p; p++) {
        char buf[2] = { *p, '\0' };
        Value v; v.type = VAL_STRING; v.ival = 0; v.fval = 0; v.sval = buf;
        vm_array_push(vm, aidx, &v);
    }
    Value arrv; arrv.type = VAL_ARRAY; arrv.ival = aidx + 1; arrv.fval = 0; arrv.sval = NULL;
    if (vm_cur_sp(vm) < 1023) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = arrv;
    }
    free(s);
    return 1;
}

/* ord(s)：返回字符串首字符的 ASCII ??*/
static int builtin_ord(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    char *s = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : "");
    pop(vm);
    push_int(vm, s[0] ? (unsigned char)s[0] : 0);
    free(s);
    return 1;
}

/* chr(n)：ASCII 码转字符 */
static int builtin_chr(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    int n = vm_cur_stack(vm)[vm_cur_sp(vm)].ival;
    pop(vm);
    char buf[2] = { (char)(n & 0xFF), '\0' };
    push_string(vm, buf);
    return 1;
}

/* keys(d)：返回字典的所有键组成的数??*/
static int builtin_keys(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    int aidx = -1;
    if (v->type == VAL_DICT) aidx = v->ival - 1;
    pop(vm);
    int out = vm_array_new(vm);
    if (out < 0) { push_nil(vm); return 1; }
    if (aidx >= 0 && aidx < vm->arrayCount) {
        VM_LOCK(vm);
        ArrayObj *a = vm_pool_slot(vm, aidx);
        for (int i = 0; i + 1 < a->count; i += 2)
            vm_array_push(vm, out, &a->items[i]);
        VM_UNLOCK(vm);
    }
    Value arrv; arrv.type = VAL_ARRAY; arrv.ival = out + 1; arrv.fval = 0; arrv.sval = NULL;
    if (vm_cur_sp(vm) < 1023) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = arrv;
    }
    return 1;
}

/* has(d, key)：判断键是否存在 */
static int builtin_has(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value key = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value *dv = &vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    int found = 0;
    if (dv->type == VAL_DICT) {
        int a = dv->ival - 1;
        if (a >= 0 && a < vm->arrayCount) {
            VM_LOCK(vm);
            ArrayObj *arr = vm_pool_slot(vm, a);
            for (int i = 0; i + 1 < arr->count; i += 2) {
                if (val_eq(&arr->items[i], &key)) { found = 1; break; }
            }
            VM_UNLOCK(vm);
        }
    }
    pop(vm); pop(vm);
    push_bool(vm, found != 0);
    return 1;
}

/* remove(d, key)：删除键（返回是否删除成功） */
static int builtin_remove(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value key = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value *dv = &vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    int removed = 0;
    if (dv->type == VAL_DICT) {
        int a = dv->ival - 1;
        if (a >= 0 && a < vm->arrayCount) removed = vm_dict_remove(vm, a, &key) ? 1 : 0;
    } else if (dv->type == VAL_ARRAY) {
        int a = dv->ival - 1;
        if (a >= 0 && a < vm->arrayCount && key.type == VAL_INT) {
            VM_LOCK(vm);
            ArrayObj *ar = vm_pool_slot(vm, a);
            int idx = (int)key.ival;
            if (ar && idx >= 0 && idx < ar->count) {
                value_free(&ar->items[idx]);
                for (int j = idx; j < ar->count - 1; j++) ar->items[j] = ar->items[j + 1];
                ar->count--;
                removed = 1;
            }
            VM_UNLOCK(vm);
        }
    }
    pop(vm); pop(vm);
    push_bool(vm, removed != 0);
    return 1;
}

/* substr(s, start, len)：取子串（负 start 从尾部算，越界自动裁剪） */
/* ---------- string methods (optimization round) ---------- */
static int builtin_str_replace(VM *vm) {
    if (vm_cur_sp(vm) < 2) return 0;
    char *ns = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : "");
    char *os = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)-1].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)-1].sval : "");
    char *s = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)-2].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)-2].sval : "");
    pop(vm); pop(vm); pop(vm);
    size_t sl = strlen(s), ol = strlen(os), nl = strlen(ns);
    if (!ol) { push_string(vm, s); free(ns); free(os); free(s); return 1; }
    size_t cap = sl + nl + 64, len = 0;
    char *out = malloc(cap ? cap : 64);
    if (!out) { push_string(vm, ""); free(ns); free(os); free(s); return 1; }
    for (size_t i = 0; i < sl; ) {
        if (i + ol <= sl && memcmp(s + i, os, ol) == 0) {
            while (len + nl + 1 > cap) { cap *= 2; out = realloc(out, cap); }
            memcpy(out + len, ns, nl); len += nl;
            i += ol;
        } else {
            while (len + 2 > cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = s[i++];
        }
    }
    out[len] = 0;
    push_string(vm, out);
    free(out); free(ns); free(os); free(s);
    return 1;
}
static int builtin_str_startswith(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    char *pf = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : "");
    char *s = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)-1].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)-1].sval : "");
    pop(vm); pop(vm);
    int ok = strncmp(s, pf, strlen(pf)) == 0;
    free(s); free(pf);
    push_int(vm, ok);
    return 1;
}
static int builtin_str_endswith(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    char *sf = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : "");
    char *s = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)-1].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)-1].sval : "");
    pop(vm); pop(vm);
    size_t sl = strlen(s), fl = strlen(sf);
    int ok = fl <= sl && memcmp(s + sl - fl, sf, fl) == 0;
    free(s); free(sf);
    push_int(vm, ok);
    return 1;
}
static int builtin_str_trim(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    char *s = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : "");
    pop(vm);
    size_t a = 0, b = strlen(s);
    while (a < b && (s[a] == ' ' || s[a] == 9 || s[a] == 10 || s[a] == 13)) a++;
    while (b > a && (s[b-1] == ' ' || s[b-1] == 9 || s[b-1] == 10 || s[b-1] == 13)) b--;
    char *out = malloc(b - a + 1);
    memcpy(out, s + a, b - a); out[b - a] = 0;
    push_string(vm, out);
    free(out); free(s);
    return 1;
}
static int builtin_str_upper(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    char *s = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : "");
    pop(vm);
    for (char *p = s; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    push_string(vm, s);
    free(s);
    return 1;
}
static int builtin_str_lower(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    char *s = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : "");
    pop(vm);
    for (char *p = s; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    push_string(vm, s);
    free(s);
    return 1;
}
static int builtin_str_index(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    char *sub = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : "");
    char *s = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)-1].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)-1].sval : "");
    pop(vm); pop(vm);
    char *p = strstr(s, sub);
    int idx = p ? (int)(p - s) : -1;
    free(s); free(sub);
    push_int(vm, idx);
    return 1;
}
static int builtin_substr(VM *vm) {
    if (vm_cur_sp(vm) < 2) return 0;
    int len = vm_cur_stack(vm)[vm_cur_sp(vm)].ival;
    int start = vm_cur_stack(vm)[vm_cur_sp(vm)-1].ival;
    /* 先拷贝（pop 会释放栈上字符串），避免悬垂指针 */
    char *s = strdup(vm_cur_stack(vm)[vm_cur_sp(vm)-2].sval ? vm_cur_stack(vm)[vm_cur_sp(vm)-2].sval : "");
    pop(vm); pop(vm); pop(vm);
    size_t sl = strlen(s);
    if (start < 0) start = (int)sl + start;
    if (start < 0) start = 0;
    if (start > (int)sl) start = (int)sl;
    if (len < 0) len = 0;
    if (start + len > (int)sl) len = (int)sl - start;
    char *buf = malloc((size_t)len + 1);
    memcpy(buf, s + start, (size_t)len);
    buf[len] = '\0';
    free(s);
    push_string(vm, buf);
    free(buf);
    return 1;
}

/* type(v)：返回值类型名（int/float/string/bool/array/dict/nil??*/
static int builtin_type(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *n;
    switch (v->type) {
        case VAL_INT: n = "int"; break;
        case VAL_FLOAT: n = "float"; break;
        case VAL_STRING: n = "string"; break;
        case VAL_BOOL: n = "bool"; break;
        case VAL_NIL: pop(vm); push_nil(vm); return 1; /* uninitialized -> null */
        case VAL_ARRAY: n = "array"; break;
        case VAL_DICT: n = "dict"; break;
        case VAL_SET: n = "set"; break;
        default: n = "unknown"; break;
    }
    pop(vm);
    push_string(vm, n);
    return 1;
}

/* args()：命令行参数数组（argv[0] = 脚本路径之后的第一个参数） */
static int builtin_args(VM *vm) {
    int aidx = vm_array_new(vm);
    if (aidx < 0) { push_nil(vm); return 1; }
    for (int i = 0; i < vm->argc; i++) {
        Value v; v.type = VAL_STRING; v.ival = 0; v.fval = 0;
        v.sval = strdup(vm->argv && vm->argv[i] ? vm->argv[i] : "");
        vm_array_push(vm, aidx, &v);
        free(v.sval);
    }
    Value arrv; arrv.type = VAL_ARRAY; arrv.ival = aidx + 1; arrv.fval = 0; arrv.sval = NULL;
    if (vm_cur_sp(vm) < 1023) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = arrv;
    }
    return 1;
}

/* ==================== vm_exec: 运行时动态构建字节码并执??自举支持) ==================== */

static Bytecode *bc_from_data(VM *vm, Value data) {
    Bytecode *bc = calloc(1, sizeof(Bytecode));
    if (!bc) return NULL;
    bytecode_init(bc);
    Value key;
    key.type = VAL_STRING; key.ival = 0; key.fval = 0; key.sval = "strings";
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
            Value nk; nk.type = VAL_STRING; nk.ival = 0; nk.fval = 0; nk.sval = "n";
            Value nm = vm_dict_get(vm, f.ival - 1, &nk);
            bc->func_names[bc->func_count] = (nm.type == VAL_STRING && nm.sval) ? strdup(nm.sval) : strdup("func");
            Value ak; ak.type = VAL_STRING; ak.ival = 0; ak.fval = 0; ak.sval = "a";
            Value av = vm_dict_get(vm, f.ival - 1, &ak);
            bc->func_argc[bc->func_count] = (av.type == VAL_INT) ? av.ival : 0;
            bc->func_count++;
        }
    }
    /* threads ??线程字节?? */
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
            Value nk2; nk2.type = VAL_STRING; nk2.ival = 0; nk2.fval = 0; nk2.sval = "n";
            Value nm2 = vm_dict_get(vm, f.ival - 1, &nk2);
            bc->thread_names[bc->thread_count] = (nm2.type == VAL_STRING && nm2.sval) ? strdup(nm2.sval) : strdup("th");
            Value ak2; ak2.type = VAL_STRING; ak2.ival = 0; ak2.fval = 0; ak2.sval = "a";
            Value av2 = vm_dict_get(vm, f.ival - 1, &ak2);
            bc->thread_argc[bc->thread_count] = (av2.type == VAL_INT) ? av2.ival : 0;
            bc->thread_count++;
        }
    }
    return bc;
}

/* ================= 网络与硬件控??简明语?? 内置函数) ================= */
/* http_get(url) / http_post(url, data): 返回响应文本 */
static int builtin_http_req(VM *vm, int is_post) {
    if (vm_cur_sp(vm) < (is_post ? 1 : 0)) return 0;
    char *data = NULL;
    if (is_post) {
        Value dv = vm_cur_stack(vm)[vm_cur_sp(vm)];
        data = strdup(dv.sval ? dv.sval : "");
    }
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm) - (is_post ? 1 : 0)];
    char *url = strdup(v.sval ? v.sval : "");
    vm_cur_set_sp(vm, vm_cur_sp(vm) - (is_post ? 2 : 1));
    char out[65536] = {0}; out[0] = '\0';
    HINTERNET hI = WinHttpOpen(L"Inimerse", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (hI) {
        URL_COMPONENTS uc = {0}; uc.dwStructSize = sizeof(uc);
        WCHAR host[256] = {0}, path[2048] = {0}, wurl[4096] = {0};
        uc.lpszHostName = host; uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 4096);
        if (WinHttpCrackUrl(wurl, 0, 0, &uc)) {
            HINTERNET hC = WinHttpConnect(hI, uc.lpszHostName, uc.nPort, 0);
            if (hC) {
                HINTERNET hR = WinHttpOpenRequest(hC, is_post ? L"POST" : L"GET",
                    uc.lpszUrlPath ? uc.lpszUrlPath : L"/", NULL, NULL, NULL,
                    (is_post ? WINHTTP_FLAG_SECURE : 0));
                if (hR) {
                    DWORD dl = is_post ? (DWORD)strlen(data) : 0;
                    if (WinHttpSendRequest(hR, NULL, 0, is_post ? data : NULL, dl, dl, 0)
                        && WinHttpReceiveResponse(hR, NULL)) {
                        DWORD avail = 0, total = 0;
                        while (WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
                            char buf[8192]; DWORD rd = 0;
                            if (!WinHttpReadData(hR, buf, avail < 8192 ? avail : 8192, &rd)) break;
                            if (rd + total < sizeof(out)) { memcpy(out + total, buf, rd); total += rd; }
                            else break;
                        }
                        out[total] = '\0';
                    }
                    WinHttpCloseHandle(hR);
                }
                WinHttpCloseHandle(hC);
            }
        }
        WinHttpCloseHandle(hI);
    }
    free(url);
    if (data) free(data);
    push_string(vm, out);
    return 1;
}
static int builtin_http_get(VM *vm) { return builtin_http_req(vm, 0); }
static int builtin_http_post(VM *vm) { return builtin_http_req(vm, 1); }

/* exec(cmd): 执行系统命令并返回输??*/
static int builtin_exec(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    char *cmd = strdup(v.sval ? v.sval : "");
    pop(vm);
    char out[65536] = {0}; out[0] = '\0';
    FILE *f = _popen(cmd, "r");
    if (f) {
        size_t total = 0, rd;
        while (total < sizeof(out) - 1 && (rd = fread(out + total, 1, 4096, f)) > 0) total += rd;
        out[total] = '\0';
        _pclose(f);
    }
    free(cmd);
    push_string(vm, out);
    return 1;
}

/* 串口: serial_open(port, baud) -> 句柄; serial_write(h, data); serial_read(h, maxlen); serial_close(h) */
static int builtin_serial_open(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value bv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value pv = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    int baud = (bv.type == VAL_INT) ? bv.ival : 9600;
    char *port = strdup(pv.sval ? pv.sval : "COM1");
    WCHAR wport[64]; MultiByteToWideChar(CP_ACP, 0, port, -1, wport, 64);
    HANDLE h = CreateFileW(wport, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    free(port);
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 2);
    if (h == INVALID_HANDLE_VALUE) { push_int(vm, -1); return 1; }
    DCB dcb = {0}; dcb.DCBlength = sizeof(DCB);
    GetCommState(h, &dcb);
    dcb.BaudRate = baud; dcb.ByteSize = 8; dcb.StopBits = ONESTOPBIT; dcb.Parity = NOPARITY;
    SetCommState(h, &dcb);
    COMMTIMEOUTS to = {0}; to.ReadIntervalTimeout = 50;
    to.ReadTotalTimeoutMultiplier = 10; to.ReadTotalTimeoutConstant = 100;
    SetCommTimeouts(h, &to);
    push_int(vm, (int)(intptr_t)h);
    return 1;
}
static int builtin_serial_write(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value hv = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value dv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    HANDLE h = (HANDLE)(intptr_t)hv.ival;
    DWORD written = 0;
    if (dv.type == VAL_STRING && dv.sval) WriteFile(h, dv.sval, (DWORD)strlen(dv.sval), &written, NULL);
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 2);
    push_int(vm, (int)written);
    return 1;
}
static int builtin_serial_read(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value hv = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value nv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    HANDLE h = (HANDLE)(intptr_t)hv.ival;
    int maxlen = (nv.type == VAL_INT) ? nv.ival : 256;
    if (maxlen < 1) maxlen = 1;
    if (maxlen > 65536) maxlen = 65536;
    char *buf = malloc(maxlen + 1);
    DWORD rd = 0;
    ReadFile(h, buf, maxlen, &rd, NULL);
    buf[rd] = '\0';
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 2);
    push_string(vm, buf);
    free(buf);
    return 1;
}
static int builtin_serial_close(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value hv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    if (hv.type == VAL_INT && hv.ival > 0) CloseHandle((HANDLE)(intptr_t)hv.ival);
    pop(vm);
    push_int(vm, 1);
    return 1;
}

/* 键鼠模拟: key_press(name); mouse_move(x,y); mouse_click(button) */
static int builtin_key_press(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *key = v.sval ? v.sval : "";
    SHORT vk = 0;
    if (strlen(key) == 1) vk = VkKeyScanA(key[0]) & 0xFF;
    else {
        struct { const char *n; int vk; } km[] = {
            {"enter", VK_RETURN}, {"space", VK_SPACE}, {"tab", VK_TAB}, {"esc", VK_ESCAPE},
            {"backspace", VK_BACK}, {"delete", VK_DELETE}, {"insert", VK_INSERT},
            {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT},
            {"home", VK_HOME}, {"end", VK_END}, {"pgup", VK_PRIOR}, {"pgdn", VK_NEXT},
            {"ctrl", VK_CONTROL}, {"alt", VK_MENU}, {"shift", VK_SHIFT},
            {"win", VK_LWIN}, {"caps", VK_CAPITAL},
            {"f1", VK_F1}, {"f2", VK_F2}, {"f3", VK_F3}, {"f4", VK_F4},
            {"f5", VK_F5}, {"f6", VK_F6}, {"f7", VK_F7}, {"f8", VK_F8},
            {"f9", VK_F9}, {"f10", VK_F10}, {"f11", VK_F11}, {"f12", VK_F12},
            {NULL, 0}
        };
        for (int i = 0; km[i].n; i++) if (strcmp(km[i].n, key) == 0) { vk = (SHORT)km[i].vk; break; }
    }
    if (vk) {
        INPUT in = {0}; in.type = INPUT_KEYBOARD; in.ki.wVk = (WORD)vk;
        SendInput(1, &in, sizeof(in));
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(in));
    }
    pop(vm);
    push_int(vm, vk ? 1 : 0);
    return 1;
}
static int builtin_mouse_move(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value yv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value xv = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    int x = (xv.type == VAL_INT) ? xv.ival : 0;
    int y = (yv.type == VAL_INT) ? yv.ival : 0;
    SetCursorPos(x, y);
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 2);
    push_int(vm, 1);
    return 1;
}
static int builtin_mouse_click(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *btn = (v.type == VAL_STRING && v.sval) ? v.sval : "left";
    DWORD dw = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP;
    if (strcmp(btn, "right") == 0) dw = MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP;
    else if (strcmp(btn, "middle") == 0) dw = MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP;
    mouse_event(dw, 0, 0, 0, 0);
    pop(vm);
    push_int(vm, 1);
    return 1;
}

/* usage(): return dict of current resource usage and declared limits */
static int builtin_usage(VM *vm) {
    int out = vm_array_new(vm);
    if (out < 0) { push_nil(vm); return 1; }
    double now = (vm->t_start) ? (double)(im_platform_now_ms() - vm->t_start) / 1000.0 : 0.0;
    const char *keys[7] = { "mem", "mem_limit", "threads", "threads_limit", "time", "time_limit", "inst_limit" };
    double vals[7];
    vals[0] = vm->used_mem;
    vals[1] = vm->limit_mem;
    vals[2] = (double)vm->active_threads;
    vals[3] = (double)vm->limit_threads;
    vals[4] = now;
    vals[5] = vm->limit_time;
    vals[6] = vm->limit_inst;
    for (int i = 0; i < 7; i++) {
        Value k; k.type = VAL_STRING; k.ival = 1; k.fval = 0;
        const char *ks = vm_intern(vm, keys[i]);
        k.sval = (char*)(ks ? ks : keys[i]);
        Value vv; vv.type = VAL_FLOAT; vv.fval = vals[i]; vv.ival = 0; vv.sval = NULL;
        vm_dict_set(vm, out, &k, &vv);
    }
    Value arrv; arrv.type = VAL_DICT; arrv.ival = out + 1; arrv.fval = 0; arrv.sval = NULL;
    if (vm_cur_sp(vm) < 1023) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = arrv;
    }
    return 1;
}

static int builtin_vm_exec(VM *vm) {

    if (vm_cur_sp(vm) < 0) return 0;
    Value data = vm_cur_stack(vm)[vm_cur_sp(vm)];
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    if (data.type != VAL_DICT) { push_int(vm, 0); return 1; }
    Bytecode *bc = bc_from_data(vm, data);
    if (!bc) { push_int(vm, 0); return 1; }
    /* 保存全局??注意??sizeof(vm->globals) 对应的大??不能声明??Value 数组——会越界破坏?? */
    GlobalSlot *saved_globals = vm->globals;
    int saved_gc = vm->globalCount;
    int saved_cap = vm->globalCap;
    int *saved_be = vm->be_bound;
    int saved_be_cap = vm->be_bound_cap;
    VmThread *saved_t = vm_get_cur_thread();
    Bytecode *saved_code = vm->code;
    vm->globals = NULL; vm->globalCount = 0; vm->globalCap = 0;
    vm->be_bound = NULL; vm->be_bound_cap = 0;
    vm_global_clone(vm);   /* independent copy of the current table (names copied, values shared via pool refs) */
    vm_load_bytecode(vm, bc);
    vm_run(vm);
    vm_set_cur_thread(saved_t);
    vm->code = saved_code;
    for (int i = 0; i < vm->globalCount; i++) value_free(&vm->globals[i].val);
    for (int i = 0; i < vm->globalCount; i++) free(vm->globals[i].name);
    free(vm->globals);
    free(vm->be_bound);
    vm->globals = saved_globals;
    vm->globalCount = saved_gc;
    vm->globalCap = saved_cap;
    vm->be_bound = saved_be;
    vm->be_bound_cap = saved_be_cap;
    bytecode_free(bc);
    free(bc);
    push_int(vm, 1);
    return 1;
}
/* ---- tiny regex engine (search semantics; ^ $ anchors; | () . * + ? [...] \d \w \s + escapes) ---- */
static const char *re_find_close(const char *re) {
    int depth = 0;
    for (const char *p = re; *p; p++) {
        if (*p == '\\') { p++; continue; }
        if (*p == '(') depth++;
        else if (*p == ')') { depth--; if (depth == 0) return p; }
    }
    return NULL;
}

static int re_class_char(const char **pp, int ch) {
    const char *p = *pp;
    if (*p == '[') {
        p++;
        int neg = 0;
        if (*p == '^') { neg = 1; p++; }
        int hit = 0;
        while (*p && *p != ']') {
            if (*p == '\\') {
                p++;
                int sub = 0;
                switch (*p) {
                    case 'd': sub = (ch >= '0' && ch <= '9'); break;
                    case 'w': sub = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'; break;
                    case 's': sub = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'); break;
                    case 'D': sub = !(ch >= '0' && ch <= '9'); break;
                    case 'W': sub = !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'); break;
                    case 'S': sub = !(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'); break;
                    default: sub = (ch == *p); break;
                }
                if (sub) hit = 1;
                p++;
            } else if (p[1] == '-' && p[2] && p[2] != ']') {
                if (ch >= (unsigned char)p[0] && ch <= (unsigned char)p[2]) hit = 1;
                p += 3;
            } else {
                if ((unsigned char)ch == (unsigned char)*p) hit = 1;
                p++;
            }
        }
        if (*p == ']') p++;
        *pp = p;
        return neg ? !hit : hit;
    }
    if (*p == '\\') {
        p++;
        int rc = 0;
        switch (*p) {
            case 'd': rc = (ch >= '0' && ch <= '9'); break;
            case 'w': rc = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'; break;
            case 's': rc = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'); break;
            case 'D': rc = !(ch >= '0' && ch <= '9'); break;
            case 'W': rc = !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'); break;
            case 'S': rc = !(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'); break;
            default: rc = (ch == *p); break;
        }
        p++;
        *pp = p;
        return rc;
    }
    *pp = p + 1;
    return (unsigned char)ch == (unsigned char)*p;
}

static const char *re_seq(const char *re, const char *s);

static const char *re_match_elem(const char *re, const char *s, const char **after) {
    if (re[0] == '.') { if (*s) { *after = re + 1; return s + 1; } return NULL; }
    if (re[0] == '[' || (re[0] == '\\' && re[1] != '\0')) {
        const char *p = re;
        if (*s && re_class_char(&p, (unsigned char)*s)) { *after = p; return s + 1; }
        return NULL;
    }
    if (re[0] == '(') {
        const char *close = re_find_close(re);
        if (!close) return NULL;
        const char *end = re_seq(re + 1, s);
        if (end) { *after = close + 1; return end; }
        return NULL;
    }
    if ((unsigned char)re[0] == (unsigned char)*s) { *after = re + 1; return s + 1; }
    return NULL;
}

static const char *re_seq(const char *re, const char *s) {
    for (;;) {
        if (re[0] == '\0' || re[0] == ')') return s;
        if (re[0] == '^') { re++; continue; }
        if (re[0] == '$') return (*s == '\0') ? re_seq(re + 1, s) : NULL;
        /* alternation: top-level '|' */
        {
            int depth = 0;
            int alt = -1;
            for (int q = 0; re[q]; q++) {
                if (re[q] == '\\') { q++; continue; }
                if (re[q] == '(') depth++;
                else if (re[q] == ')') depth--;
                else if (re[q] == '|' && depth == 0) { alt = q; break; }
            }
            if (alt >= 0) {
                char *left = (char*)malloc((size_t)alt + 1);
                memcpy(left, re, (size_t)alt); left[alt] = '\0';
                const char *m = re_seq(left, s);
                free(left);
                if (m) return m;
                return re_seq(re + alt + 1, s);
            }
        }
        /* element length (escapes / classes / groups span >1 chars) */
        int elen = 1;
        if (re[0] == '\\' && re[1]) elen = 2;
        else if (re[0] == '[') {
            int d = 1;
            while (re[d] && re[d] != ']') { if (re[d] == '\\') d++; d++; }
            if (re[d] == ']') d++;
            elen = d;
        } else if (re[0] == '(') {
            const char *cl = re_find_close(re);
            if (!cl) return NULL;
            elen = (int)(cl - re) + 1;
        }
        int qmin = 1, qmax = 1;
        const char *elnext;
        if (re[elen] == '*') { qmin = 0; qmax = 1000000000; elnext = re + elen + 1; }
        else if (re[elen] == '+') { qmin = 1; qmax = 1000000000; elnext = re + elen + 1; }
        else if (re[elen] == '?') { qmin = 0; qmax = 1; elnext = re + elen + 1; }
        else elnext = re + elen;
        /* greedy consume, then backtrack from longest */
        const char *t = s;
        int count = 0;
        const char *pos[4096];
        while (count < qmax) {
            const char *ae;
            const char *nt = re_match_elem(re, t, &ae);
            if (!nt) break;
            if (count < 4096) pos[count] = nt;
            t = nt;
            count++;
        }
        if (count < qmin) return NULL;
        for (;;) {
            const char *cont = re_seq(elnext, t);
            if (cont) return cont;
            if (count <= qmin) return NULL;
            count--;
            t = pos[count - 1];
        }
    }
}

static int regex_match(const char *pattern, const char *s) {
    if (!pattern || !s) return 0;
    if (pattern[0] == '^') return re_seq(pattern + 1, s) != NULL;
    for (const char *t = s; ; t++) {
        if (re_seq(pattern, t)) return 1;
        if (*t == '\0') break;
    }
    return 0;
}

static int builtin_match(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value *pat = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value *str = &vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    char sbuf[512];
    const char *s;
    if (str->type == VAL_STRING && str->sval) s = str->sval;
    else { vm_value_to_string(vm, str, sbuf, sizeof sbuf); s = sbuf; }
    const char *re = (pat->type == VAL_STRING && pat->sval) ? pat->sval : "";
    int ok = regex_match(re, s);
    pop(vm); pop(vm);
    push_bool(vm, ok != 0);
    return 1;
}

static int builtin_range(VM *vm) {
    if (vm_cur_sp(vm) < 1) return 0;
    Value *gi = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    int gidx = (gi->type == VAL_INT) ? gi->ival : -1;
    pop(vm); pop(vm);
    /* be-bound global: return the be set */
    if (gidx >= 0 && gidx < vm->be_bound_cap && vm->be_bound[gidx] > 0) {
        int bidx = vm->be_bound[gidx] - 1;
        if (bidx >= 0 && bidx < vm->setCount) {
            Value sv; sv.type = VAL_SET; sv.ival = bidx; sv.fval = 0; sv.sval = NULL;
            { int _sp = vm_cur_sp(vm); vm_cur_stack(vm)[_sp + 1] = sv; vm_cur_set_sp(vm, _sp + 1); }
            return 1;
        }
    }
    if (v->type == VAL_SET) {
        Value sv = *v;
            { int _sp = vm_cur_sp(vm); vm_cur_stack(vm)[_sp + 1] = sv; vm_cur_set_sp(vm, _sp + 1); }
        return 1;
    }
    if (v->type == VAL_INT) {
        int sidx = vm_set_new(vm);
        if (sidx < 0) { push_nil(vm); return 1; }
        SetObj *s = &vm->sets[sidx];
        s->kind = 2; s->nameIdx = 1;
        s->lo = -2147483648.0; s->hi = 2147483647.0;
        s->loInc = 1; s->hiInc = 1;
        Value sv; sv.type = VAL_SET; sv.ival = sidx; sv.fval = 0; sv.sval = NULL;
            { int _sp = vm_cur_sp(vm); vm_cur_stack(vm)[_sp + 1] = sv; vm_cur_set_sp(vm, _sp + 1); }
        return 1;
    }
    if (v->type == VAL_FLOAT) {
        int sidx = vm_set_new(vm);
        if (sidx < 0) { push_nil(vm); return 1; }
        SetObj *s = &vm->sets[sidx];
        s->kind = 2; s->nameIdx = 24; /* R: all reals */
        s->lo = -1e308; s->hi = 1e308;
        s->loInc = 0; s->hiInc = 0;
        Value sv; sv.type = VAL_SET; sv.ival = sidx; sv.fval = 0; sv.sval = NULL;
            { int _sp = vm_cur_sp(vm); vm_cur_stack(vm)[_sp + 1] = sv; vm_cur_set_sp(vm, _sp + 1); }
        return 1;
    }
    push_nil(vm);
    return 1;
}

/* ---------- .params parameter file support ---------- */
static int builtin_load_params(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    Value pv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    char *path = strdup(pv.type == VAL_STRING && pv.sval ? pv.sval : "");
    if (pv.type == VAL_STRING && pv.ival != 1 && pv.sval) free(pv.sval);
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    int rc = vm_params_load(vm, path);
    free(path);
    push_int(vm, rc == 0 ? 1 : 0);
    return 1;
}

static int builtin_list_params(VM *vm) {
    int aidx = vm_array_new(vm);
    if (aidx < 0) { push_nil(vm); return 1; }
    for (int i = 0; i < vm->globalCount; i++) {
        const char *nm = vm->globals[i].name;
        if (!nm || !nm[0]) continue;
        if (strncmp(nm, "u.", 2) == 0) continue;          /* system namespace */
        if (!strchr(nm, '.')) continue;                   /* dotted (namespace) params only */
        Value v; v.type = VAL_STRING; v.sval = (char*)nm; v.ival = 0; v.fval = 0;
        vm_array_push(vm, aidx, &v);
    }
    Value arrv; arrv.type = VAL_ARRAY; arrv.ival = aidx + 1; arrv.fval = 0; arrv.sval = NULL;
    if (vm_cur_sp(vm) < 1023) { vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = arrv; }
    else push_nil(vm);
    return 1;
}

static int builtin_save_params(VM *vm) {
    if (vm_cur_sp(vm) < 0) return 0;
    int argc = vm_cur_sp(vm) + 1;
    Value pv = vm_cur_stack(vm)[0];
    char *path = strdup(pv.type == VAL_STRING && pv.sval ? pv.sval : "");
    char **names = NULL; int ncount = 0;
    for (int i = 1; i < argc; i++) {
        Value v = vm_cur_stack(vm)[i];
        if (v.type == VAL_STRING && v.sval) {
            names = realloc(names, (ncount + 1) * sizeof(char*));
            names[ncount++] = strdup(v.sval);
        }
    }
    if (ncount == 0) {
        /* no explicit names: save all dotted globals (except system u.*) */
        for (int i = 0; i < vm->globalCount; i++) {
            const char *nm = vm->globals[i].name;
            if (!nm || !nm[0]) continue;
            if (strncmp(nm, "u.", 2) == 0) continue;
            if (!strchr(nm, '.')) continue;
            names = realloc(names, (ncount + 1) * sizeof(char*));
            names[ncount++] = strdup(nm);
        }
    }
    /* free string args */
    for (int i = 0; i < argc; i++) {
        Value v = vm_cur_stack(vm)[i];
        if (v.type == VAL_STRING && v.ival != 1 && v.sval) free(v.sval);
    }
    vm_cur_set_sp(vm, -1);
    /* read original file, replace matching "name =" values, keep comments/structure */
    char **out = NULL; int outlen = 0;
    FILE *f = fopen(path, "rb");
    if (f) {
        char line[4096];
        while (fgets(line, sizeof line, f)) {
            size_t llen = strlen(line);
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            int matched = 0;
            for (int i = 0; i < ncount && !matched; i++) {
                size_t nl = strlen(names[i]);
                if (strncmp(p, names[i], nl) == 0 && (p[nl] == ' ' || p[nl] == '\t' || p[nl] == '=')) {
                    char *eq = strchr(p, '=');
                    if (eq) {
                        char *hash = strchr(eq + 1, '#');
                        Value gv; gv.type = VAL_NIL; int gidx = -1;
                        for (int g = 0; g < vm->globalCount; g++)
                            if (vm->globals[g].name && strcmp(vm->globals[g].name, names[i]) == 0) { gv = vm->globals[g].val; gidx = g; break; }
                        if (gidx >= 0) {
                            char vbuf[1024];
                            vm_value_to_string(vm, &gv, vbuf, sizeof vbuf);
                            size_t pre = (size_t)(eq - line) + 1;  /* include '=' */
                            char nline[4300];
                            int np = 0;
                            memcpy(nline, line, pre); np = (int)pre;
                            nline[np++] = ' ';
                            size_t vl = strlen(vbuf);
                            memcpy(nline + np, vbuf, vl); np += (int)vl;
                            if (hash) { nline[np++] = ' '; size_t cl = 0; while (hash[cl] && hash[cl] != '\n') cl++; memcpy(nline + np, hash, cl); np += (int)cl; }
                            if (llen > 0 && line[llen - 1] == '\n') nline[np++] = '\n';
                            nline[np] = '\0';
                            out = realloc(out, (outlen + 1) * sizeof(char*));
                            out[outlen++] = strdup(nline);
                            matched = 1;
                        }
                    }
                }
            }
            if (!matched) { out = realloc(out, (outlen + 1) * sizeof(char*)); out[outlen++] = strdup(line); }
        }
        fclose(f);
    }
    if (outlen == 0) {
        /* file missing: create from scratch for requested names */
        for (int i = 0; i < ncount; i++) {
            Value gv; gv.type = VAL_NIL; int gidx = -1;
            for (int g = 0; g < vm->globalCount; g++)
                if (vm->globals[g].name && strcmp(vm->globals[g].name, names[i]) == 0) { gv = vm->globals[g].val; gidx = g; break; }
            if (gidx >= 0) {
                char vbuf[1024]; vm_value_to_string(vm, &gv, vbuf, sizeof vbuf);
                char nline[4300];
                snprintf(nline, sizeof nline, "%s = %s\n", names[i], vbuf);
                out = realloc(out, (outlen + 1) * sizeof(char*));
                out[outlen++] = strdup(nline);
            }
        }
    }
    FILE *wf = fopen(path, "wb");
    int ok = 0;
    if (wf) {
        for (int i = 0; i < outlen; i++) { fputs(out[i], wf); free(out[i]); }
        fclose(wf); ok = 1;
    }
    if (out) free(out);
    for (int i = 0; i < ncount; i++) free(names[i]);
    if (names) free(names);
    free(path);
    push_int(vm, ok);
    return 1;
}



/* ---------- L1 modular SPI: capability query + mod meta (minimal-permission) ---------- */
/* caps string parser: "io,net,ai" -> bitmask ("all" = full) */
static int spi_parse_caps(const char *s) {
    int caps = 0;
    const char *p = s ? s : "";
    while (*p) {
        if (strncmp(p, "io", 2) == 0) caps |= CAP_IO;
        else if (strncmp(p, "net", 3) == 0) caps |= CAP_NET;
        else if (strncmp(p, "ai", 2) == 0) caps |= CAP_AI;
        else if (strncmp(p, "verse", 5) == 0) caps |= CAP_VERSE;
        else if (strncmp(p, "dbg", 3) == 0) caps |= CAP_DBG;
        else if (strncmp(p, "proc", 4) == 0) caps |= CAP_PROC;
        else if (strncmp(p, "all", 3) == 0) caps |= CAP_MASK;
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return caps;
}
static int builtin_spi_meta(VM *vm) { /* spi_meta(id, version, caps): declare mod identity + capabilities */
    if (vm_cur_sp(vm) < 2) return 0;
    Value *capsv = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value *verv = &vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    Value *idv = &vm_cur_stack(vm)[vm_cur_sp(vm) - 2];
    const char *id = (idv->type == VAL_STRING && idv->sval) ? idv->sval : "anon";
    int version = (verv->type == VAL_INT) ? verv->ival : (int)verv->fval;
    int caps = 0;
    if (capsv->type == VAL_STRING) caps = spi_parse_caps(capsv->sval);
    else if (capsv->type == VAL_INT) caps = capsv->ival;
    else if (capsv->type == VAL_BOOL) caps = capsv->ival ? CAP_MASK : 0;
    vm->mod_caps = caps; /* replace declared mask (0 = no capabilities at all) */
    int found = -1;
    for (int i = 0; i < vm->modCount; i++) if (strcmp(vm->mods[i].id, id) == 0) { found = i; break; }
    if (found < 0) {
        if (vm->modCount < 32) {
            snprintf(vm->mods[vm->modCount].id, sizeof(vm->mods[0].id), "%s", id);
            vm->mods[vm->modCount].version = version;
            vm->mods[vm->modCount].api_min = 0;
            vm->mods[vm->modCount].caps = caps;
            vm->modCount++;
        }
    } else {
        vm->mods[found].version = version;
        vm->mods[found].caps = caps;
    }
    pop(vm); pop(vm); pop(vm);
    return 1;
}
/* spi_on(event, funcname) -> 1/0: register a script function as event callback.
   The callback runs as a task (async) with the emitted data as its single argument. */
static int builtin_spi_on(VM *vm) {
    int sp0 = vm_cur_sp(vm);
    if (sp0 < 1) { push_int(vm, 0); return 1; }
    Value fv = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value ev = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    const char *func = (fv.type == VAL_STRING && fv.sval) ? fv.sval : "";
    const char *event = (ev.type == VAL_STRING && ev.sval) ? ev.sval : "";
    /* pop both args (free non-intern strings) */
    vm_cur_set_sp(vm, sp0 - 2);
    if (fv.type == VAL_STRING && fv.ival != 1 && fv.sval) free(fv.sval);
    if (ev.type == VAL_STRING && ev.ival != 1 && ev.sval) free(ev.sval);
    Bytecode *bc = vm->code;
    int tidx = -1;
    if (bc) for (int i = 0; i < bc->thread_count; i++)
        if (bc->thread_names[i] && strcmp(bc->thread_names[i], func) == 0) { tidx = i; break; }
    if (tidx < 0) { push_int(vm, 0); return 1; }  /* func not found */
    VM_LOCK(vm);
    if (vm->spi_sub_count >= vm->spi_sub_cap) {
        int nc = vm->spi_sub_cap ? vm->spi_sub_cap * 2 : 8;
        SpiSub *ns = (SpiSub*)realloc(vm->spi_subs, (size_t)nc * sizeof(SpiSub));
        if (ns) { vm->spi_subs = ns; vm->spi_sub_cap = nc; }
    }
    int ok = 0;
    if (vm->spi_sub_count < vm->spi_sub_cap) {
        vm->spi_subs[vm->spi_sub_count].event = strdup(event);
        vm->spi_subs[vm->spi_sub_count].tidx = tidx;
        vm->spi_sub_count++;
        ok = 1;
    }
    VM_UNLOCK(vm);
    push_int(vm, ok);
    return 1;
}

/* spi_emit(event, data) -> int fired count. Each callback runs as an async task
   with the emitted data as its argument (strings are interned so the task stays valid). */
/* spi_emit(event, data) -> int fired count. Each subscribed task is started as an
   async task; the data is published into the global __spi_data (strings interned). */
static int builtin_spi_emit(VM *vm) {
    int sp0 = vm_cur_sp(vm);
    if (sp0 < 1) { push_int(vm, 0); return 1; }
    Value data = vm_cur_stack(vm)[vm_cur_sp(vm)];
    Value ev = vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    const char *event = (ev.type == VAL_STRING && ev.sval) ? ev.sval : "";
    Value arg;
    if (data.type == VAL_STRING) {
        const char *si = vm_intern(vm, data.sval ? data.sval : "");
        arg.type = VAL_STRING; arg.ival = 1; arg.fval = 0; arg.sval = (char*)si;
    } else arg = data;
    /* publish to global __spi_data (task callbacks read it) */
    for (int gi = 0; gi < vm->globalCount; gi++) {
        if (strcmp(vm->globals[gi].name, "__spi_data") == 0) { vm->globals[gi].val = arg; break; }
    }
    VmThread *t = vm_get_cur_thread();
    int fired = 0;
    for (int i = 0; i < vm->spi_sub_count; i++) {
        if (strcmp(vm->spi_subs[i].event, event) != 0) continue;
        VmThread *nt = vm_os_thread_start(vm, vm->code, vm->spi_subs[i].tidx, t, 0);
        if (nt) {
            /* synchronous dispatch on an OS thread (no Fiber scheduler dependency):
               wait for the callback thread, 3s guard. */
            WaitForSingleObject(nt->os_handle, 3000);
            fired++;
        }
    }
    vm_cur_set_sp(vm, sp0 - 2);
    if (data.type == VAL_STRING && data.ival != 1 && data.sval) free(data.sval);
    if (ev.type == VAL_STRING && ev.ival != 1 && ev.sval) free(ev.sval);
    push_int(vm, fired);
    return 1;
}
static int builtin_spi_caps(VM *vm) { /* spi_caps() -> int bitmask of declared caps (-1 = unrestricted) */
    push_int(vm, vm->mod_caps);
    return 1;
}
static int builtin_spi_has(VM *vm) { /* spi_has(name) -> 1 if builtin exists and is callable with declared caps */
    if (vm_cur_sp(vm) < 0) return 0;
    Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *name = (v->type == VAL_STRING && v->sval) ? v->sval : "";
    int idx = builtin_lookup(vm, name);
    int ok = 0;
    if (idx >= 0) {
        if (vm->mod_caps >= 0 && (vm->builtins[idx].flags & CAP_MASK) &&
            (vm->mod_caps & vm->builtins[idx].flags) == 0) ok = 0;
        else if (vm->safe_mode && (vm->builtins[idx].flags & 1)) ok = 0;
        else ok = 1;
    }
    pop(vm);
    push_int(vm, ok);
    return 1;
}
static int builtin_spi_mods(VM *vm) { /* spi_mods() -> array of {id, version, caps} */
    int aidx = vm_array_new(vm);
    if (aidx >= 0) {
        for (int i = 0; i < vm->modCount; i++) {
            int didx = vm_array_new(vm);
            if (didx < 0) continue;
            Value k, vv;
            k.type = VAL_STRING; k.ival = 1; k.fval = 0; k.sval = "id";
            vv.type = VAL_STRING; vv.ival = 1; vv.fval = 0; vv.sval = vm->mods[i].id;
            vm_array_push(vm, didx, &k); vm_array_push(vm, didx, &vv);
            k.sval = "version"; vv.type = VAL_INT; vv.ival = vm->mods[i].version; vv.fval = 0; vv.sval = NULL;
            vm_array_push(vm, didx, &k); vm_array_push(vm, didx, &vv);
            k.sval = "caps"; vv.type = VAL_INT; vv.ival = vm->mods[i].caps; vv.fval = 0; vv.sval = NULL;
            vm_array_push(vm, didx, &k); vm_array_push(vm, didx, &vv);
            Value dv; dv.type = VAL_DICT; dv.ival = didx + 1; dv.fval = 0; dv.sval = NULL;
            vm_array_push(vm, aidx, &dv);
        }
    }
    Value av; av.type = VAL_ARRAY; av.ival = aidx + 1; av.fval = 0; av.sval = NULL;
    if (vm_cur_sp(vm) < 1023) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = av;
    }
    return 1;
}


/* ---------- GC control (mark-sweep for pool slots, opt-in) ---------- */
/* ================= entity system (SoA + spatial grid) ================= */
static double ent_arg(VM *vm, int i) {
    Value v = vm_cur_stack(vm)[i];
    if (v.type == VAL_INT) return (double)v.ival;
    if (v.type == VAL_FLOAT) return v.fval;
    return 0.0;
}
static void ent_popn(VM *vm, int n) {
    int sp = vm_cur_sp(vm);
    vm_cur_set_sp(vm, sp - n);
}
static int ent_bucket_idx(VM *vm, float x, float y) {
    int bx = (int)(x / ENTITY_CELL); if (bx < 0) bx = 0; if (bx >= ENTITY_GRID_W) bx = ENTITY_GRID_W - 1;
    int by = (int)(y / ENTITY_CELL); if (by < 0) by = 0; if (by >= ENTITY_GRID_H) by = ENTITY_GRID_H - 1;
    return bx + by * ENTITY_GRID_W;
}
static void ent_bucket_append(VM *vm, int b, int id) {
    EntBucket *bk = ((EntBucket*)vm->ent_buckets) + b;
    if (bk->count >= bk->cap) {
        int nc = bk->cap == 0 ? 4 : bk->cap * 2;
        bk->ids = realloc(bk->ids, (size_t)nc * sizeof(int));
        bk->cap = nc;
    }
    bk->ids[bk->count++] = id;
}
static void ent_grid_clear(VM *vm) {
    EntBucket *bks = (EntBucket*)vm->ent_buckets;
    if (!bks) return;
    for (int i = 0; i < ENTITY_GRID_W * ENTITY_GRID_H; i++) bks[i].count = 0;
}
static void ent_grid_rebuild(VM *vm) {
    EntBucket *bks = (EntBucket*)vm->ent_buckets;
    if (!bks) return;
    ent_grid_clear(vm);
    for (int i = 0; i < vm->ent_count; i++) {
        if (vm->ent_hp[i] < 0) continue;
        int b = ent_bucket_idx(vm, vm->ent_x[i], vm->ent_y[i]);
        ent_bucket_append(vm, b, i);
    }
    vm->ent_grid_dirty = 0;
}
static void ent_ensure_grid(VM *vm) {
    if (vm->ent_grid_dirty || !vm->ent_buckets) {
        if (!vm->ent_buckets) {
            int nb = ENTITY_GRID_W * ENTITY_GRID_H;
            vm->ent_buckets = calloc((size_t)nb, sizeof(EntBucket));
            if (!vm->ent_free) vm->ent_free = malloc(1024 * sizeof(int));
            vm->ent_free_head = -1;
        }
        ent_grid_rebuild(vm);
    }
}
static int builtin_entity_spawn(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 3) return 0;
    double x = ent_arg(vm, argc - 3), y = ent_arg(vm, argc - 2);
    int kind = (int)ent_arg(vm, argc - 1);
    ent_popn(vm, argc);
    int id;
    if (vm->ent_free_head >= 0) {
        id = vm->ent_free_head;
        vm->ent_free_head = vm->ent_free[id];
    } else {
        if (vm->ent_count >= vm->ent_cap) {
            int nc = vm->ent_cap == 0 ? 1024 : vm->ent_cap * 2;
            vm->ent_x = realloc(vm->ent_x, nc * sizeof(float));
            vm->ent_y = realloc(vm->ent_y, nc * sizeof(float));
            vm->ent_vx = realloc(vm->ent_vx, nc * sizeof(float));
            vm->ent_vy = realloc(vm->ent_vy, nc * sizeof(float));
            vm->ent_hp = realloc(vm->ent_hp, nc * sizeof(int));
            vm->ent_kind = realloc(vm->ent_kind, nc * sizeof(int));

            vm->ent_free = realloc(vm->ent_free, nc * sizeof(int));
            vm->ent_cap = nc;
        }
        id = vm->ent_count++;
    }
    vm->ent_x[id] = (float)x; vm->ent_y[id] = (float)y;
    vm->ent_vx[id] = 0; vm->ent_vy[id] = 0;
    vm->ent_hp[id] = 1; vm->ent_kind[id] = kind;
    vm->ent_grid_dirty = 1;
    if (vm->limit_mem > 0) vm->used_mem += 96;
    push_int(vm, id);
    return 1;
}
static int builtin_entity_kill(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    int id = (int)ent_arg(vm, argc - 1);
    ent_popn(vm, argc);
    if (id < 0 || id >= vm->ent_count || vm->ent_hp[id] < 0) { push_int(vm, 0); return 1; }
    vm->ent_hp[id] = -1;
    vm->ent_free[id] = vm->ent_free_head;
    vm->ent_free_head = id;
    vm->ent_grid_dirty = 1;
    if (vm->limit_mem > 0) vm->used_mem -= 96;
    push_int(vm, 1);
    return 1;
}
static int builtin_entity_count(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    ent_popn(vm, argc);
    push_int(vm, vm->ent_count);
    return 1;
}
static int builtin_entity_clear(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    ent_popn(vm, argc);
    vm->ent_count = 0;
    vm->ent_free_head = -1;
    ent_grid_clear(vm);
    vm->ent_grid_dirty = 0;
    push_int(vm, 1);
    return 1;
}
static int builtin_entity_set(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 3) return 0;
    int id = (int)ent_arg(vm, argc - 3);
    Value *kv = &vm_cur_stack(vm)[vm_cur_sp(vm) - 1];
    const char *key = (kv->type == VAL_STRING) ? kv->sval : "";
    double val = ent_arg(vm, argc - 1);
    ent_popn(vm, argc);
    if (id < 0 || id >= vm->ent_count || vm->ent_hp[id] < 0) { push_int(vm, 0); return 1; }
    if (strcmp(key, "x") == 0) { vm->ent_x[id] = (float)val; vm->ent_grid_dirty = 1; }
    else if (strcmp(key, "y") == 0) { vm->ent_y[id] = (float)val; vm->ent_grid_dirty = 1; }
    else if (strcmp(key, "vx") == 0) vm->ent_vx[id] = (float)val;
    else if (strcmp(key, "vy") == 0) vm->ent_vy[id] = (float)val;
    else if (strcmp(key, "hp") == 0) vm->ent_hp[id] = (int)val;
    else if (strcmp(key, "kind") == 0) vm->ent_kind[id] = (int)val;
    else { push_int(vm, 0); return 1; }
    push_int(vm, 1);
    return 1;
}
static int builtin_entity_get(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 2) return 0;
    int id = (int)ent_arg(vm, argc - 2);
    Value *kv = &vm_cur_stack(vm)[vm_cur_sp(vm)];
    const char *key = (kv->type == VAL_STRING) ? kv->sval : "";
    ent_popn(vm, argc);
    if (id < 0 || id >= vm->ent_count || vm->ent_hp[id] < 0) { push_nil(vm); return 1; }
    if (strcmp(key, "x") == 0) push_float(vm, vm->ent_x[id]);
    else if (strcmp(key, "y") == 0) push_float(vm, vm->ent_y[id]);
    else if (strcmp(key, "vx") == 0) push_float(vm, vm->ent_vx[id]);
    else if (strcmp(key, "vy") == 0) push_float(vm, vm->ent_vy[id]);
    else if (strcmp(key, "hp") == 0) push_int(vm, vm->ent_hp[id]);
    else if (strcmp(key, "kind") == 0) push_int(vm, vm->ent_kind[id]);
    else push_nil(vm);
    return 1;
}
static int builtin_entity_neighbors(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 3) return 0;
    double x = ent_arg(vm, argc - 3), y = ent_arg(vm, argc - 2), r = ent_arg(vm, argc - 1);
    ent_popn(vm, argc);
    int aidx = vm_array_new(vm);
    if (aidx < 0) { push_nil(vm); return 1; }
    float rr = (float)(r * r);
    ent_ensure_grid(vm);
    int cells = (int)(r / ENTITY_CELL) + 1; if (cells < 1) cells = 1;
    int cx = (int)(x / ENTITY_CELL), cy = (int)(y / ENTITY_CELL);
    EntBucket *bks = (EntBucket*)vm->ent_buckets;
    for (int dy = -cells; dy <= cells; dy++) {
        int by = cy + dy; if (by < 0 || by >= ENTITY_GRID_H) continue;
        for (int dx = -cells; dx <= cells; dx++) {
            int bx = cx + dx; if (bx < 0 || bx >= ENTITY_GRID_W) continue;
            EntBucket *bk = bks + (bx + by * ENTITY_GRID_W);
            for (int k = 0; k < bk->count; k++) {
                int e = bk->ids[k];
                if (vm->ent_hp[e] < 0) continue;
                float ddx = vm->ent_x[e] - (float)x, ddy = vm->ent_y[e] - (float)y;
                if (ddx * ddx + ddy * ddy <= rr) {
                    Value v; v.type = VAL_INT; v.ival = e; v.fval = 0; v.sval = NULL;
                    vm_array_push(vm, aidx, &v);
                }
            }
        }
    }
    Value av; av.type = VAL_ARRAY; av.ival = aidx + 1; av.fval = 0; av.sval = NULL;
    { int _sp = vm_cur_sp(vm); vm_cur_stack(vm)[_sp + 1] = av; vm_cur_set_sp(vm, _sp + 1); }
    return 1;
}
static int builtin_entity_at(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 2) return 0;
    double x = ent_arg(vm, argc - 2), y = ent_arg(vm, argc - 1);
    ent_popn(vm, argc);
    ent_ensure_grid(vm);
    int b = ent_bucket_idx(vm, (float)x, (float)y);
    EntBucket *bk = ((EntBucket*)vm->ent_buckets) + b;
    for (int k = 0; k < bk->count; k++) {
        int e = bk->ids[k];
        if (vm->ent_hp[e] < 0) continue;
        push_int(vm, e);
        return 1;
    }
    push_int(vm, -1);
    return 1;
}
static int builtin_gc_auto(VM *vm) { /* gc_auto(1|0): enable/disable auto GC */
    int argc = vm_cur_sp(vm) + 1;
    if (argc >= 1) {
        Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
        int on = (v->type == VAL_INT) ? v->ival : (int)v->fval;
        vm->gc_enabled = on ? 1 : 0;
        if (on && vm->gc_threshold <= 0) vm->gc_threshold = 2.0 * 1024 * 1024;
    }
    push_int(vm, vm->gc_enabled);
    return 1;
}
static int builtin_gc_now(VM *vm) { /* gc_now(): collect at next safe point (immediate if single-threaded) */
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
/* ---------- 鍘熷瓙鎿嶄綔锛堝叏灞�?int 鍘熷瓙璇?鏀?鍐欙紱涓?LOAD/STORE_GLOBAL 鍚屽垎鐗囬攣浜掓枼锛?---------- */
static int builtin_atomic_add(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 2) return 0;
    Value *st = vm_cur_stack(vm);
    Value *nv = &st[vm_cur_sp(vm) - 1];
    const char *nm = (nv->type == VAL_STRING) ? nv->sval : NULL;
    Value *dv = &st[vm_cur_sp(vm)];
    long long delta = (dv->type == VAL_INT) ? (long long)dv->ival : (dv->type == VAL_FLOAT ? (long long)dv->fval : 0);
    vm_cur_set_sp(vm, vm_cur_sp(vm) - argc);
    if (!nm) { push_int(vm, 0); return 1; }
    int idx = -1;
    for (int i = 0; i < vm->globalCount; i++)
        if (vm->globals[i].name && strcmp(vm->globals[i].name, nm) == 0) { idx = i; break; }
    if (idx < 0) {
        VM_LOCK(vm);
        for (int i = 0; i < vm->globalCount; i++)
            if (vm->globals[i].name && strcmp(vm->globals[i].name, nm) == 0) { idx = i; break; }
        if (idx < 0) {
            if (vm->globalCount >= vm->globalCap) vm_global_grow(vm, vm->globalCount);
            idx = vm->globalCount;
            vm->globals[idx].name = strdup(nm);
            vm->globals[idx].val.type = VAL_INT; vm->globals[idx].val.ival = 0;
            vm->globals[idx].val.fval = 0; vm->globals[idx].val.sval = NULL;
            vm->globalCount++;
        }
        VM_UNLOCK(vm);
    }
    if (vm->active_threads > 1) im_mutex_lock((ImMutex*)VM_GSHARD(vm, idx));
    if (vm->globals[idx].val.type != VAL_INT) { vm->globals[idx].val.type = VAL_INT; vm->globals[idx].val.ival = 0; }
    LONG old = InterlockedExchangeAdd((volatile LONG*)&vm->globals[idx].val.ival, (LONG)delta);
    if (vm->active_threads > 1) im_mutex_unlock((ImMutex*)VM_GSHARD(vm, idx));
    push_int(vm, (int)(old + delta));
    return 1;
}
static int builtin_atomic_get(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 1) return 0;
    Value *st = vm_cur_stack(vm);
    Value *nv = &st[vm_cur_sp(vm)];
    const char *nm = (nv->type == VAL_STRING) ? nv->sval : NULL;
    vm_cur_set_sp(vm, vm_cur_sp(vm) - argc);
    int idx = -1;
    for (int i = 0; i < vm->globalCount; i++)
        if (vm->globals[i].name && strcmp(vm->globals[i].name, nm) == 0) { idx = i; break; }
    if (idx < 0) { push_int(vm, 0); return 1; }
    if (vm->active_threads > 1) im_mutex_lock((ImMutex*)VM_GSHARD(vm, idx));
    int v = (vm->globals[idx].val.type == VAL_INT) ? vm->globals[idx].val.ival : 0;
    if (vm->active_threads > 1) im_mutex_unlock((ImMutex*)VM_GSHARD(vm, idx));
    push_int(vm, v);
    return 1;
}
static int builtin_atomic_set(VM *vm) {
    int argc = vm_cur_sp(vm) + 1;
    if (argc < 2) return 0;
    Value *st = vm_cur_stack(vm);
    Value *nv = &st[vm_cur_sp(vm) - 1];
    const char *nm = (nv->type == VAL_STRING) ? nv->sval : NULL;
    Value *vv = &st[vm_cur_sp(vm)];
    long long val = (vv->type == VAL_INT) ? (long long)vv->ival : (vv->type == VAL_FLOAT ? (long long)vv->fval : 0);
    vm_cur_set_sp(vm, vm_cur_sp(vm) - argc);
    int idx = -1;
    for (int i = 0; i < vm->globalCount; i++)
        if (vm->globals[i].name && strcmp(vm->globals[i].name, nm) == 0) { idx = i; break; }
    if (idx < 0) {
        VM_LOCK(vm);
        for (int i = 0; i < vm->globalCount; i++)
            if (vm->globals[i].name && strcmp(vm->globals[i].name, nm) == 0) { idx = i; break; }
        if (idx < 0) {
            if (vm->globalCount >= vm->globalCap) vm_global_grow(vm, vm->globalCount);
            idx = vm->globalCount;
            vm->globals[idx].name = strdup(nm);
            vm->globals[idx].val.type = VAL_INT; vm->globals[idx].val.ival = 0;
            vm->globals[idx].val.fval = 0; vm->globals[idx].val.sval = NULL;
            vm->globalCount++;
        }
        VM_UNLOCK(vm);
    }
    if (vm->active_threads > 1) im_mutex_lock((ImMutex*)VM_GSHARD(vm, idx));
    vm->globals[idx].val.type = VAL_INT; vm->globals[idx].val.ival = (int)val;
    vm->globals[idx].val.fval = 0; vm->globals[idx].val.sval = NULL;
    if (vm->active_threads > 1) im_mutex_unlock((ImMutex*)VM_GSHARD(vm, idx));
    push_int(vm, (int)val);
    return 1;
}
static int builtin_gc_stats(VM *vm) { /* gc_stats() -> {runs, freed, enabled, threshold, used} */
    int aidx = vm_array_new(vm);
    if (aidx < 0) { push_nil(vm); return 1; }
    const char *ks[5] = { "runs", "freed", "enabled", "threshold", "used" };
    long long vs[5];
    vs[0] = vm->gc_runs; vs[1] = vm->gc_freed; vs[2] = vm->gc_enabled;
    vs[3] = (long long)vm->gc_threshold; vs[4] = (long long)vm->used_mem;
    for (int i = 0; i < 5; i++) {
        Value k, vv;
        k.type = VAL_STRING; k.ival = 1; k.fval = 0; k.sval = (char*)ks[i];
        vv.type = VAL_INT; vv.ival = (int)vs[i]; vv.fval = 0; vv.sval = NULL;
        vm_array_push(vm, aidx, &k);
        vm_array_push(vm, aidx, &vv);
    }
    Value dv; dv.type = VAL_DICT; dv.ival = aidx + 1; dv.fval = 0; dv.sval = NULL;
    if (vm_cur_sp(vm) < 1023) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = dv;
    }
    return 1;
}
/* mod_limit(mem_mb, vram_mb, time_s): set resource quotas (0 = unlimited); returns 1.
   mod_usage(): dict {mem, time} current consumption (vram/inst not tracked yet). */
static int builtin_mod_limit(VM *vm) {
    int sp0 = vm_cur_sp(vm);
    double mem_mb = 0, vram_mb = 0, time_s = 0;
    if (sp0 >= 0) { Value v = vm_cur_stack(vm)[vm_cur_sp(vm)]; if (v.type == VAL_INT) time_s = (double)v.ival; else if (v.type == VAL_FLOAT) time_s = v.fval; }
    if (sp0 >= 1) { Value v = vm_cur_stack(vm)[vm_cur_sp(vm) - 1]; if (v.type == VAL_INT) vram_mb = (double)v.ival; else if (v.type == VAL_FLOAT) vram_mb = v.fval; }
    if (sp0 >= 2) { Value v = vm_cur_stack(vm)[vm_cur_sp(vm) - 2]; if (v.type == VAL_INT) mem_mb = (double)v.ival; else if (v.type == VAL_FLOAT) mem_mb = v.fval; }
    vm_cur_set_sp(vm, vm_cur_sp(vm) - (sp0 + 1));
    vm->limit_mem = (mem_mb > 0) ? mem_mb * 1048576.0 : 0;
    vm->limit_vram = (vram_mb > 0) ? vram_mb * 1048576.0 : 0;
    vm->limit_time = time_s;
    push_int(vm, 1);
    return 1;
}
static int builtin_mod_usage(VM *vm) {
    double tsec = (vm->t_start > 0) ? (double)(im_platform_now_ms() - vm->t_start) / 1000.0 : 0;
    int aidx = vm_array_new(vm);
    Value k, val;
    k.type = VAL_STRING; k.ival = 0; k.fval = 0; k.sval = strdup("mem");
    val.type = VAL_FLOAT; val.fval = vm->used_mem; val.ival = 0; val.sval = NULL;
    vm_dict_set(vm, aidx, &k, &val); free(k.sval);
    k.sval = strdup("time");
    val.type = VAL_FLOAT; val.fval = tsec;
    vm_dict_set(vm, aidx, &k, &val); free(k.sval);
    Value dv; dv.type = VAL_DICT; dv.ival = aidx + 1; dv.fval = 0; dv.sval = NULL;
    if (vm_cur_sp(vm) < 1023) { vm_cur_set_sp(vm, vm_cur_sp(vm) + 1); vm_cur_stack(vm)[vm_cur_sp(vm)] = dv; }
    return 1;
}

void runtime_register_builtins(VM *vm) {
    srand((unsigned)time(NULL));
    vm_register_builtin(vm, "sqrt", builtin_sqrt);
    vm_register_builtin(vm, "round", builtin_round);
    vm_register_builtin(vm, "int", builtin_int);
    vm_register_builtin(vm, "float", builtin_float);
    vm_register_builtin(vm, "str", builtin_str);
    vm_register_builtin(vm, "bool", builtin_bool);
    vm_register_builtin(vm, "len", builtin_len);
    vm_register_builtin(vm, "size", builtin_size);
    vm_register_builtin(vm, "list", builtin_list);
    vm_register_builtin(vm, "sum", builtin_sum);
    vm_register_builtin(vm, "push", builtin_push);
    vm_register_builtin(vm, "pop", builtin_pop);
    vm_register_builtin(vm, "join", builtin_join);
    vm_register_builtin(vm, "split", builtin_split);
    vm_register_builtin(vm, "chars", builtin_chars);
    vm_register_builtin(vm, "ord", builtin_ord);
    vm_register_builtin(vm, "chr", builtin_chr);
    vm_register_builtin(vm, "keys", builtin_keys);
    vm_register_builtin(vm, "has", builtin_has);
    vm_register_builtin(vm, "remove", builtin_remove);
    vm_register_builtin(vm, "substr", builtin_substr);
    vm_register_builtin(vm, "replace", builtin_str_replace);
    vm_register_builtin(vm, "startswith", builtin_str_startswith);
    vm_register_builtin(vm, "endswith", builtin_str_endswith);
    vm_register_builtin(vm, "trim", builtin_str_trim);
    vm_register_builtin(vm, "upper", builtin_str_upper);
    vm_register_builtin(vm, "lower", builtin_str_lower);
    vm_register_builtin(vm, "index", builtin_str_index);
    vm_register_builtin(vm, "type", builtin_type);
    vm_register_builtin(vm, "args", builtin_args);
    vm_register_builtin_full(vm, "vm_exec", builtin_vm_exec, 1|CAP_DBG|CAP_PROC, 0);

    vm_register_builtin(vm, "usage", builtin_usage);
    vm_register_builtin(vm, "match", builtin_match);
    vm_register_builtin(vm, "range", builtin_range);
    vm_register_builtin_full(vm, "load_params", builtin_load_params, 1|CAP_IO, 0);
    vm_register_builtin_full(vm, "save_params", builtin_save_params, 1|CAP_IO, 0);
    vm_register_builtin(vm, "list_params", builtin_list_params);
    vm_register_builtin(vm, "spi_meta", builtin_spi_meta);
    vm_register_builtin(vm, "spi_on", builtin_spi_on);
    vm_register_builtin(vm, "spi_emit", builtin_spi_emit);
    vm_register_builtin(vm, "mod_limit", builtin_mod_limit);
    vm_register_builtin(vm, "mod_usage", builtin_mod_usage);    vm_register_builtin(vm, "spi_caps", builtin_spi_caps);
    vm_register_builtin(vm, "spi_has", builtin_spi_has);
    vm_register_builtin(vm, "spi_mods", builtin_spi_mods);

    vm_register_builtin(vm, "gc_auto", builtin_gc_auto);
    vm_register_builtin(vm, "gc_now", builtin_gc_now);
    vm_register_builtin(vm, "atomic_add", builtin_atomic_add);
    vm_register_builtin(vm, "atomic_get", builtin_atomic_get);
    vm_register_builtin(vm, "atomic_set", builtin_atomic_set);
    vm_register_builtin(vm, "entity_spawn", builtin_entity_spawn);
    vm_register_builtin(vm, "entity_kill", builtin_entity_kill);
    vm_register_builtin(vm, "entity_set", builtin_entity_set);
    vm_register_builtin(vm, "entity_get", builtin_entity_get);
    vm_register_builtin(vm, "entity_count", builtin_entity_count);
    vm_register_builtin(vm, "entity_clear", builtin_entity_clear);
    vm_register_builtin(vm, "entity_neighbors", builtin_entity_neighbors);
    vm_register_builtin(vm, "entity_at", builtin_entity_at);    vm_register_builtin(vm, "gc_stats", builtin_gc_stats);
}
