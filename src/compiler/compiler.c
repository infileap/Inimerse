#include "compiler.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int next_register = 1;
static int lambda_counter = 0;
static int reg_peak = 0;
static const char **lambda_outer_names = NULL;
static int lambda_outer_count = 0;
static int alloc_reg(void) { if (next_register >= 1024) { fprintf(stderr, "\n[error] registers overflow (>=1024), abort compile.\n"); exit(1); } if (next_register > reg_peak) reg_peak = next_register; return next_register++; }
static void reset_regs(void) { next_register = 1; reg_peak = 0; }

/* 涓存椂鍙橀噺浼樺寲锟?
   - protect_reg(r)锛氭妸 r 鎻愬崌涓烘寔涔呭瘎瀛樺櫒锛堣法璇彞瀛樻椿锛屽寰幆鏁扮粍/缁撴潫鍊硷級
   - release_temps()锛氬洖鏀舵墍鏈夊尶鍚嶄复鏃跺瘎瀛樺櫒锛堝洖閫€姘翠綅鍒版寔涔呮按浣嶏級锟?
     璇彞缂栬瘧缁撴潫/琛ㄨ揪寮忓瓙鑺傜偣娑堣垂鍚庤皟锟?*/
static void protect_reg(Compiler *comp, int r) {
    if (r + 1 > comp->local_peak) comp->local_peak = r + 1;
}

static void release_temps(Compiler *comp) {
    if (next_register > comp->local_peak)
        next_register = comp->local_peak;
    if (next_register < 1) next_register = 1;  /* 瀵勫瓨锟?淇濈暀锛岄伩鍏嶉《灞傝剼锟?release 锟?alloc_reg 杩斿洖0 */
}

/* 鍥為€€姘翠綅锟?level锛堥噴鏀捐姘翠綅涔嬪悗鐨勫尶鍚嶄复鏃跺瘎瀛樺櫒锛夛紝浣嗕笉浣庝簬鎸佷箙姘翠綅 */
static void release_to(Compiler *comp, int level) {
    if (next_register > level) next_register = level;
    if (next_register < comp->local_peak) next_register = comp->local_peak;
    if (next_register < 1) next_register = 1;  /* 瀵勫瓨锟?淇濈暀 */
}

/* 鎶婂眬閮ㄥ彉閲忔敞鍐岃繘 locals 琛紙鍒嗛厤瀵勫瓨鍣ㄥ苟鎻愬崌鎸佷箙姘翠綅锟?*/
static int alloc_local(Compiler *comp, const char *name) {
    int r = alloc_reg();
    if (comp->localCount < 1024) {
        comp->locals[comp->localCount].name = strdup(name);
        comp->locals[comp->localCount].reg = r;
        comp->localCount++;
    } else {
        fprintf(stderr, "[error] local variable limit 1024 exceeded ('%s').\n", name);
        exit(1);
    }
    protect_reg(comp, r);
    return r;
}

static void compile_stmt(Compiler *comp, Stmt *stmt, int **break_list, int *break_count_ptr);
static int  compile_expr(Compiler *comp, Expr *expr);
static void emit(Bytecode *bc, OpCode op, int r1, int r2, int r3);

/* ---- record helpers (compiler) ---- */
static int comp_record_is_reg(Compiler *comp, int gidx) {
    return (gidx >=0 && gidx < comp->record_flags_cap && comp->record_flags[gidx]);
}
static void comp_record_mark(Compiler *comp, int gidx) {
    if (gidx >= comp->record_flags_cap) {
        int nc = comp->record_flags_cap ==0 ? 16 : comp->record_flags_cap *2;
        while (nc <= gidx) nc *=2;
        comp->record_flags = realloc(comp->record_flags, nc * sizeof(int));
        for (int i = comp->record_flags_cap; i < nc; i++) comp->record_flags[i] =0;
        comp->record_flags_cap = nc;
    }
    comp->record_flags[gidx] =1;
}
/* pack store|scope<<2|merge<<4 from tags (string literals) */
static int comp_record_meta(Compiler *comp, RecordTag *tags, int tagCount) {
    (void)comp;
    int store =0, scope =0, merge =0;
    for (int i =0; i < tagCount; i++) {
        if (!tags[i].value || tags[i].value->type != EXPR_STRING) continue;
        char vbuf[32];
        int vl = tags[i].value->stringVal.length <31 ? tags[i].value->stringVal.length :31;
        memcpy(vbuf, tags[i].value->stringVal.start, vl);
        vbuf[vl] = '\0';
        if (strcmp(tags[i].key, "store") ==0) {
            if (strcmp(vbuf, "local") ==0) store =1;
            else if (strcmp(vbuf, "server") ==0) store =2;
            else if (strcmp(vbuf, "both") ==0) store =3;
        } else if (strcmp(tags[i].key, "scope") ==0) {
            if (strcmp(vbuf, "entity") ==0) scope =1;
        } else if (strcmp(tags[i].key, "merge") ==0) {
            if (strcmp(vbuf, "sum") ==0) merge =1;
        }
    }
    return store | (scope <<2) | (merge <<4);
}

/* const helpers */
static void comp_const_mark(Compiler *comp, int gidx) {
    if (gidx >= comp->const_flags_cap) {
        int nc = comp->const_flags_cap == 0 ? 64 : comp->const_flags_cap * 2;
        while (nc <= gidx) nc *= 2;
        comp->const_flags = realloc(comp->const_flags, (size_t)nc * sizeof(int));
        for (int i = comp->const_flags_cap; i < nc; i++) comp->const_flags[i] = 0;
        comp->const_flags_cap = nc;
    }
    comp->const_flags[gidx] = 1;
}
static int comp_const_is(Compiler *comp, int gidx) {
    return (gidx >= 0 && gidx < comp->const_flags_cap) ? comp->const_flags[gidx] : 0;
}

/* ---------- 鍛藉悕绌洪棿鍓嶇紑锛堢紪璇戞湡绗﹀彿闅旂锛屽彇浠?parser 鎷兼帴+rename锛?---------- */
/* 灞曞紑涓哄畬鏁寸鍙峰悕锛歝ur_ns + name锛坈ur_ns 涓?"" 鏃跺師鏍疯繑鍥烇級 */
static void ns_full(const Compiler *comp, const char *name, char *out, size_t sz) {
    if (comp->cur_ns[0])
        snprintf(out, sz, "%s%s", comp->cur_ns, name);
    else {
        size_t n = strlen(name);
        if (n >= sz) n = sz - 1;
        memcpy(out, name, n);
        out[n] = '\0';
    }
}

/* import 鐩稿璺緞瑙ｆ瀽锛堝師 parser.c 閫昏緫杩佸叆锛夛細缁濆/甯︾洏绗﹀師鏍凤紝鍚﹀垯 base_dir + rel */
static char *resolve_import_path(const char *base_dir, const char *rel) {
    if (!rel || !*rel) return strdup("");
    if (rel[0] == '/' || rel[0] == '\\' || strchr(rel, ':') != NULL)
        return strdup(rel);
    if (!base_dir || !*base_dir) return strdup(rel);
    size_t n = strlen(base_dir) + strlen(rel) + 2;
    char *out = malloc(n);
    snprintf(out, n, "%s/%s", base_dir, rel);
    return out;
}

static void dir_of(const char *path, char *out, size_t sz) {
    strncpy(out, path, sz - 1); out[sz - 1] = '\0';
    char *slash = strrchr(out, '/');
    if (!slash) slash = strrchr(out, '\\');
    if (slash) *slash = '\0';
    else out[0] = '\0';
}

/* 鎴愬憳閾?a.b.c 鎵佸钩鍖栦负鐩稿鍚嶏紝瑕佹眰鏈€澶栧眰 object 鏄?IDENT 涓?鈭?褰撳墠妯″潡鍙鍛藉悕绌洪棿 */
static int ns_flatten(const Compiler *comp, Expr *e, char *out, size_t sz) {
    char parts[8][128];
    int nparts = 0;
    Expr *cur = e;
    while (cur && cur->type == EXPR_MEMBER) {
        char m[128];
        snprintf(m, sizeof(m), "%.*s", (int)cur->member.member.length, cur->member.member.start);
        if (nparts >= 8) return 0;
        strcpy(parts[nparts++], m);
        cur = cur->member.object;
    }
    if (!cur || cur->type != EXPR_IDENT) return 0;
    char root[128];
    snprintf(root, sizeof(root), "%.*s", (int)cur->identName.length, cur->identName.start);
    int vis = 0;
    for (int i = 0; i < comp->ns_visible_count; i++)
        if (strcmp(comp->ns_visible[i], root) == 0) { vis = 1; break; }
    if (!vis) return 0;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", root);
    for (int i = nparts - 1; i >= 0; i--) {
        strncat(buf, ".", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, parts[i], sizeof(buf) - strlen(buf) - 1);
    }
    if (strlen(buf) >= sz) return 0;
    strcpy(out, buf);
    return 1;
}

int register_global(Compiler *comp, const char *name) {
    char full[512];
    ns_full(comp, name, full, sizeof(full));
    for (int i = 0; i < comp->globalCount; i++)
        if (strcmp(comp->globals[i].name, full) == 0) return comp->globals[i].index;
    if (comp->globalCount >= comp->globalCap) {
        comp->globalCap = comp->globalCap == 0 ? 16 : comp->globalCap * 2;
        comp->globals = realloc(comp->globals, comp->globalCap * sizeof(GlobalVar));
    }
    comp->globals[comp->globalCount].name = strdup(full);
    comp->globals[comp->globalCount].index = comp->globalCount;
    return comp->globalCount++;
}

/* 鍙煡鍏ㄥ眬锛堜笉鍒涘缓锛夛細杩斿洖绱㈠紩锛屼笉瀛樺湪杩斿洖 -1 */
static int lookup_global_idx(Compiler *comp, const char *name) {
    char full[512];
    ns_full(comp, name, full, sizeof(full));
    for (int i = 0; i < comp->globalCount; i++)
        if (strcmp(comp->globals[i].name, full) == 0) return comp->globals[i].index;
    return -1;
}

/* 鍑芥暟锟?global 澹版槑妫€鏌ワ細鏄惧紡澹版槑鍚庡啓鍏ㄥ眬锛屾湭澹版槑璧嬪€间竴寰嬪眬閮紙Python 寮忥級 */
static int lookup_global_decl(Compiler *comp, const char *name) {
    char full[512];
    ns_full(comp, name, full, sizeof(full));
    for (int i = 0; i < comp->gdeclCount; i++)
        if (strcmp(comp->gdecl[i], full) == 0) return i;
    return -1;
}

/* ---------- 鑷畾涔夊嚱锟?---------- */
static int register_func(Compiler *comp, const char *name) {
    char full[512];
    ns_full(comp, name, full, sizeof(full));
    for (int i = 0; i < comp->mainBC->func_count; i++)
        if (strcmp(comp->mainBC->func_names[i], full) == 0) return i;
    if (comp->mainBC->func_count >= 1024) {
        fprintf(stderr, "[error] function limit 1024 exceeded while registering '%s'.\n", full);
        exit(1);
    }
    int idx = comp->mainBC->func_count++;
    comp->mainBC->func_names[idx] = strdup(full);
    comp->mainBC->funcs[idx] = NULL;
    comp->mainBC->func_argc[idx] = 0;
    return idx;
}

static int lookup_func(Compiler *comp, const char *name) {
    char full[512];
    ns_full(comp, name, full, sizeof(full));
    for (int i = 0; i < comp->mainBC->func_count; i++)
        if (strcmp(comp->mainBC->func_names[i], full) == 0) return i;
    return -1;
}

/* ---------- 绾跨▼涓庝簰鏂ラ攣 ---------- */
static int register_thread(Compiler *comp, const char *name) {
    char full[512];
    ns_full(comp, name, full, sizeof(full));
    for (int i = 0; i < comp->mainBC->thread_count; i++)
        if (strcmp(comp->mainBC->thread_names[i], full) == 0) return i;
    if (comp->mainBC->thread_count >= 32) {
        fprintf(stderr, "[error] thread limit 32 exceeded while registering '%s'.\n", full);
        exit(1);
    }
    int idx = comp->mainBC->thread_count++;
    comp->mainBC->thread_names[idx] = strdup(full);
    comp->mainBC->threads[idx] = NULL;
    comp->mainBC->thread_argc[idx] = 0;
    comp->mainBC->thread_flags[idx] = 0;
    return idx;
}

static int lookup_thread(Compiler *comp, const char *name) {
    char full[512];
    ns_full(comp, name, full, sizeof(full));
    for (int i = 0; i < comp->mainBC->thread_count; i++)
        if (strcmp(comp->mainBC->thread_names[i], full) == 0) return i;
    return -1;
}

static int register_mutex(Compiler *comp, const char *name) {
    char full[512];
    ns_full(comp, name, full, sizeof(full));
    for (int i = 0; i < comp->mutexCount; i++)
        if (strcmp(comp->mutexes[i].name, full) == 0) return i;
    if (comp->mutexCount >= 256) {
        fprintf(stderr, "[error] mutex limit 256 exceeded while registering '%s'.\n", full);
        exit(1);
    }
    comp->mutexes[comp->mutexCount].name = strdup(full);
    comp->mutexes[comp->mutexCount].idx = comp->mutexCount;
    return comp->mutexCount++;
}

/* 鍑芥暟鍐呭眬閮ㄥ彉閲忥細杩斿洖瀵勫瓨鍣ㄥ彿锟?1 琛ㄧず璧板叏灞€ */
static int lookup_local(Compiler *comp, const char *name) {
    for (int i = 0; i < comp->localCount; i++)
        if (strcmp(comp->locals[i].name, name) == 0) return comp->locals[i].reg;
    return -1;
}

/* 鍑芥暟鍐呭垎閰嶆柊灞€閮ㄥ瘎瀛樺櫒锛堝寰幆璁℃暟鍣紝閬垮厤閲嶅悕鍐茬獊锟?*/
static int fresh_local(Compiler *comp, const char *hint) {
    return alloc_local(comp, hint);
}

/* resolve label patches for current compilation unit (to/break) and store label table into curBC */
static void resolve_labels(Compiler *comp) {
    for (int i = 0; i < comp->patchCount; i++) {
        LabelPatch *pt = &comp->patches[i];
        if (pt->kind == 2) continue; /* thread-goto resolved later */
        int off = -1;
        for (int j = 0; j < comp->labelCount; j++)
            if (strcmp(comp->labels[j].name, pt->label) == 0) {
                off = (pt->kind == 0) ? comp->labels[j].start_off : comp->labels[j].end_off;
                break;
            }
        if (off < 0) {
            fprintf(stderr, "error: unknown label '%s'\n", pt->label);
            exit(1);
        }
        comp->curBC->code[pt->jump_pos].r2 = off;
    }
    /* store label table into bytecode (for thread-goto / debug) */
    if (comp->labelCount > 0) {
        LabelEntry *le = malloc(comp->labelCount * sizeof(*le));
        for (int i = 0; i < comp->labelCount; i++) {
            le[i].name = comp->labels[i].name;
            le[i].off = comp->labels[i].start_off;
        }
        comp->curBC->labels = le;
        comp->curBC->label_count = comp->labelCount;
        comp->labels = NULL; comp->labelCount = 0;
    }
    comp->patchCount = 0;
    if (comp->patches) { free(comp->patches); comp->patches = NULL; }
}

/* resolve thread-goto patches: label lives in target thread bytecode (after all thread bodies compiled) */
static void resolve_thread_gotos(Compiler *comp) {
    for (int i = 0; i < comp->patchCount; i++) {
        LabelPatch *pt = &comp->patches[i];
        if (pt->kind != 2) continue;
        int tidx = comp->mainBC->code[pt->jump_pos].r1;
        if (tidx < 0 || tidx >= comp->mainBC->thread_count || comp->mainBC->threads[tidx] == NULL) {
            fprintf(stderr, "error: thread %d not compiled\n", tidx);
            exit(1);
        }
        Bytecode *tb = comp->mainBC->threads[tidx];
        int off = -1;
        for (int j = 0; j < tb->label_count; j++)
            if (strcmp(tb->labels[j].name, pt->label) == 0) { off = tb->labels[j].off; break; }
        if (off < 0) {
            fprintf(stderr, "error: label '%s' not found in thread %s\n", pt->label, comp->mainBC->thread_names[tidx] ? comp->mainBC->thread_names[tidx] : "?");
            exit(1);
        }
        comp->mainBC->code[pt->jump_pos].r2 = off;
    }
}

static void compile_func_body(Compiler *comp, Stmt *stmt) {
    char name[256];
    snprintf(name, sizeof(name), "%.*s", (int)stmt->funcDef.name.length, stmt->funcDef.name.start);
    int fidx = register_func(comp, name);
    if (fidx < 0) return;

    Bytecode *fbc = malloc(sizeof(Bytecode));
    bytecode_init(fbc);
    comp->curBC = fbc;
    comp->in_function = 1;
    comp->localCount = 0;
    comp->gdeclCount = 0;
    comp->local_peak = 0;
    reset_regs();
    /* 鍙傛暟缁戝畾涓哄眬閮ㄥ彉閲忥紙瀵勫瓨锟?1..argc锟?*/
    for (int i = 0; i < stmt->funcDef.paramCount; i++) {
        char pn[256];
        snprintf(pn, sizeof(pn), "%.*s", (int)stmt->funcDef.params[i].length, stmt->funcDef.params[i].start);
        alloc_local(comp, pn);
    }
    /* 缂栬瘧鍑芥暟锟?*/
    int *breaks = NULL; int bcount = 0;
    for (int i = 0; i < stmt->funcDef.bodyCount; i++)
        compile_stmt(comp, stmt->funcDef.body[i], &breaks, &bcount);
    free(breaks);
    /* 鏈熬榛樿杩斿洖 nil */
    emit(comp->curBC, OP_RETURN, 0, 0, 0);
    resolve_labels(comp);

    comp->curBC = comp->mainBC;
    comp->in_function = 0;
    comp->localCount = 0;
    comp->mainBC->funcs[fidx] = fbc;
        
    comp->mainBC->func_argc[fidx] = stmt->funcDef.paramCount;
}

static void compile_thread_body(Compiler *comp, Stmt *stmt) {
    char name[256];
    snprintf(name, sizeof(name), "%.*s", (int)stmt->threadDef.name.length, stmt->threadDef.name.start);
    int tidx = register_thread(comp, name);
    if (tidx < 0) return;

    Bytecode *tbc = malloc(sizeof(Bytecode));
    bytecode_init(tbc);
    comp->curBC = tbc;
    comp->in_function = 1;
    comp->in_thread = 1;
    comp->localCount = 0;
    comp->gdeclCount = 0;
    comp->local_peak = 0;
    reset_regs();
    /* 鍙傛暟缁戝畾涓哄眬閮ㄥ彉閲忥紙瀵勫瓨锟?1..argc锟?*/
    for (int i = 0; i < stmt->threadDef.paramCount; i++) {
        char pn[256];
        snprintf(pn, sizeof(pn), "%.*s", (int)stmt->threadDef.params[i].length, stmt->threadDef.params[i].start);
        alloc_local(comp, pn);
    }
    int *breaks = NULL; int bcount = 0;
    for (int i = 0; i < stmt->threadDef.bodyCount; i++)
        compile_stmt(comp, stmt->threadDef.body[i], &breaks, &bcount);
    free(breaks);
    /* 绾跨▼浣撴湯灏撅細HALT 琛ㄧず璇ョ嚎绋嬫墽琛屽畬锟?*/
    emit(comp->curBC, OP_HALT, 0, 0, 0);
    resolve_labels(comp);
    comp->curBC = comp->mainBC;
    comp->in_function = 0;
    comp->in_thread = 0;
    comp->localCount = 0;
    comp->mainBC->threads[tidx] = tbc;
    comp->mainBC->thread_argc[tidx] = stmt->threadDef.paramCount;
    comp->mainBC->thread_flags[tidx] = (unsigned char)stmt->threadDef.flags;
}

static int lookup_builtin(Compiler *comp, const char *name) {
    for (int i = 0; i < comp->builtinCount; i++)
        if (strcmp(comp->builtins[i].name, name) == 0) return i;
    return -1;
}

static void emit(Bytecode *bc, OpCode op, int r1, int r2, int r3) {
    if (bc->count >= bc->capacity) {
        bc->capacity = bc->capacity == 0 ? 32 : bc->capacity * 2;
        bc->code = realloc(bc->code, bc->capacity * sizeof(RegInstruction));
    }
    bc->code[bc->count].op = op;
    bc->code[bc->count].r1 = r1;
    bc->code[bc->count].r2 = r2;
    bc->code[bc->count].r3 = r3;
    bc->count++;
}

/* ---------- 琛ㄨ揪寮忕紪锟?---------- */
/* emit builtin call: push argc regs, CALL_BUILTIN fname; returns result reg */
static int emit_builtin_call(Compiler *comp, const char *fname, int argReg, int argc) {
    int w0 = next_register;
    emit(comp->curBC, OP_PUSH_REG, argReg, 0, 0);
    release_to(comp, w0);
    int result = alloc_reg();
    int ni = bytecode_add_string(comp->curBC, fname);
    emit(comp->curBC, OP_CALL_BUILTIN, result, ni, argc);
    return result;
}

/* in-place conversion of a variable: v = conv(v); returns v (also stores back) */
static int compile_inplace_cast(Compiler *comp, const char *objName, const char *convFn) {
    if (comp->in_function) {
        int local = lookup_local(comp, objName);
        if (local >= 0) {
            int v = emit_builtin_call(comp, convFn, local, 1);
            emit(comp->curBC, OP_MOV, local, v, 0);
            comp->last_temp = 0;
            return local;
        }
    }
    int g = register_global(comp, objName);
    int a = alloc_reg();
    emit(comp->curBC, OP_LOAD_GLOBAL, a, g, 0);
    int v = emit_builtin_call(comp, convFn, a, 1);
    emit(comp->curBC, OP_STORE_GLOBAL, g, v, 0);
    comp->last_temp = 1;
    return v;
}

static int compile_expr(Compiler *comp, Expr *expr) {
    if (!expr) return -1;
    switch (expr->type) {
        case EXPR_NUMBER: {
            int r = alloc_reg();
            if (expr->intVal > 2147483647LL || expr->intVal < -2147483648LL) {
                int fidx = bytecode_add_float(comp->curBC, (double)expr->intVal);
                emit(comp->curBC, OP_LOADK_FLOAT, r, fidx, 0);
                fprintf(stderr, "warning: integer literal %lld out of 32-bit range, promoted to float\r\n",
                        (long long)expr->intVal);
            } else {
                emit(comp->curBC, OP_LOADK_INT, r, (int)expr->intVal, 0);
            }
            comp->last_temp = 1;
            return r;
        }
        case EXPR_FLOAT: {
            int r = alloc_reg();
            int idx = bytecode_add_float(comp->curBC, expr->floatVal);
            emit(comp->curBC, OP_LOADK_FLOAT, r, idx, 0);
            comp->last_temp = 1;
            return r;
        }
        case EXPR_STRING: {
            int r = alloc_reg();
            int sl = (int)expr->stringVal.length;
            char *buf = malloc((size_t)sl + 1);
            memcpy(buf, expr->stringVal.start, (size_t)sl);
            buf[sl] = 0;
            int idx = bytecode_add_string(comp->curBC, buf);
            free(buf);
            emit(comp->curBC, OP_LOADK_STRING, r, idx, 0);
            comp->last_temp = 1;
            return r;
        }
        case EXPR_BOOL: {
            int r = alloc_reg();
            emit(comp->curBC, OP_LOADK_BOOL, r, expr->boolVal ? 1 : 0, 0);
            comp->last_temp = 1;
            return r;
        }
        case EXPR_IDENT: {
            char name[256];
            snprintf(name, sizeof(name), "%.*s", (int)expr->identName.length, expr->identName.start);
            int local = lookup_local(comp, name);
            if (local >= 0) { comp->last_temp = 0; return local; }
            for (int oi = 0; oi < lambda_outer_count; ++oi)
                if (strcmp(lambda_outer_names[oi], name) == 0) {
                    int ci = bytecode_add_capture(comp->curBC, name);
                    int r = alloc_reg();
                    emit(comp->curBC, OP_LOAD_CAPTURE, r, ci, 0);
                    comp->last_temp = 1;
                    return r;
                }
            int r = alloc_reg();
            int g_idx = lookup_global_idx(comp, name);  /* 璇诲彇涓嶅垱寤哄叏灞€锛氭湭瀹氫箟 -> -1 -> NIL */
            emit(comp->curBC, OP_LOAD_GLOBAL, r, g_idx, 0);
            comp->last_temp = 1;
            return r;
        }
        case EXPR_SETLIT: {
            /* each item compiled, pushed; OP_NEW_SET merges+dedups from stack */
            int w0 = next_register;
            for (int i = 0; i < expr->setlit.count; i++) {
                int r = compile_expr(comp, expr->setlit.items[i]);
                emit(comp->curBC, OP_PUSH_REG, r, 0, 0);
            }
            release_to(comp, w0);
            int r = alloc_reg();
            emit(comp->curBC, OP_NEW_SET, r, 0, expr->setlit.count);
            comp->last_temp = 1;
            return r;
        }
        case EXPR_SETCOMP: {
            int setReg = compile_expr(comp, expr->setcomp.set);
            int w0 = next_register;
            int listReg = alloc_reg();
            int li = bytecode_add_string(comp->curBC, "list");
            emit(comp->curBC, OP_PUSH_REG, setReg, 0, 0);
            emit(comp->curBC, OP_CALL_BUILTIN, listReg, li, 1);
            int nilReg = alloc_reg();
            emit(comp->curBC, OP_IS_NIL, nilReg, listReg, 0);
            int jnil = comp->curBC->count;
            emit(comp->curBC, OP_JUMP_IF_TRUE, nilReg, 0, 0);
            int result = alloc_reg();
            emit(comp->curBC, OP_NEW_SET, result, 0, 0);
            int iReg = alloc_reg();
            emit(comp->curBC, OP_LOADK_INT, iReg, 0, 0);
            int nReg = alloc_reg();
            int lni = bytecode_add_string(comp->curBC, "len");
            emit(comp->curBC, OP_PUSH_REG, listReg, 0, 0);
            emit(comp->curBC, OP_CALL_BUILTIN, nReg, lni, 1);
            int oneReg = alloc_reg();
            emit(comp->curBC, OP_LOADK_INT, oneReg, 1, 0);
            int loopStart = comp->curBC->count;
            int geReg = alloc_reg();
            emit(comp->curBC, OP_GE, geReg, iReg, nReg);
            int jdone = comp->curBC->count;
            emit(comp->curBC, OP_JUMP_IF_TRUE, geReg, 0, 0);
            int xReg = alloc_reg();
            emit(comp->curBC, OP_INDEX_GET, xReg, listReg, iReg);
            char xname[256];
            snprintf(xname, sizeof(xname), "%.*s", (int)expr->setcomp.varName.length, expr->setcomp.varName.start);
            int xIdx = register_global(comp, xname);
            emit(comp->curBC, OP_STORE_GLOBAL, xIdx, xReg, 0);
            int condReg = compile_expr(comp, expr->setcomp.cond);
            int jnext = comp->curBC->count;
            emit(comp->curBC, OP_JUMP_IF_FALSE, condReg, 0, 0);
            emit(comp->curBC, OP_SET_ADD, result, xReg, 0);
            comp->curBC->code[jnext].r2 = comp->curBC->count;
            emit(comp->curBC, OP_ADD, iReg, iReg, oneReg);
            emit(comp->curBC, OP_JUMP, 0, loopStart, 0);
            int jskip = comp->curBC->count;
            emit(comp->curBC, OP_JUMP, 0, 0, 0);
            int throwOff = comp->curBC->count;
            {
                int errReg = alloc_reg();
                int ei = bytecode_add_string(comp->curBC, "comprehension: source set is not enumerable");
                emit(comp->curBC, OP_LOADK_STRING, errReg, ei, 0);
                emit(comp->curBC, OP_THROW, errReg, 0, 0);
            }
            int doneOff = comp->curBC->count;
            comp->curBC->code[jnil].r2 = throwOff;
            comp->curBC->code[jskip].r2 = doneOff;
            comp->curBC->code[jdone].r2 = doneOff;
            release_to(comp, w0);
            comp->last_temp = 1;
            return result;
        }
        case EXPR_SETINTERVAL: {
            int r = alloc_reg();
            int loReg, hiReg;
            if (expr->setinterval.lo) loReg = compile_expr(comp, expr->setinterval.lo);
            else { loReg = alloc_reg(); emit(comp->curBC, OP_LOADK_FLOAT, loReg, bytecode_add_float(comp->curBC, -1.0e308), 0); }
            if (expr->setinterval.hi) hiReg = compile_expr(comp, expr->setinterval.hi);
            else { hiReg = alloc_reg(); emit(comp->curBC, OP_LOADK_FLOAT, hiReg, bytecode_add_float(comp->curBC, 1.0e308), 0); }
            char sname[256];
            snprintf(sname, sizeof(sname), "%.*s", (int)expr->setinterval.base.length, expr->setinterval.base.start);
            int nameIdx = bytecode_add_string(comp->curBC, sname);
            int flags = (expr->setinterval.loInc ? 1 : 0) | (expr->setinterval.hiInc ? 2 : 0);
            emit(comp->curBC, OP_PUSH_REG, loReg, 0, 0);
            emit(comp->curBC, OP_PUSH_REG, hiReg, 0, 0);
            emit(comp->curBC, OP_SET_INTERVAL, r, (nameIdx & 0xFFFFFF) | (flags << 24), 0);
            comp->last_temp = 1;
            return r;
        }
        case EXPR_BINARY: {
            if (expr->binary.op == TOK_AND || expr->binary.op == TOK_OR) {
                int left = compile_expr(comp, expr->binary.left);
                int l_temp = comp->last_temp;
                int r_wm = next_register;
                int result;
                if (l_temp) result = left;  /* 缁撴灉澶嶇敤宸︽搷浣滄暟锛堜复鏃讹級 */
                else { result = alloc_reg(); emit(comp->curBC, OP_MOV, result, left, 0); }
                int jmp_pos;
                if (expr->binary.op == TOK_AND) {
                    jmp_pos = comp->curBC->count;
                    emit(comp->curBC, OP_JUMP_IF_FALSE, result, 0, 0);
                    int right = compile_expr(comp, expr->binary.right);
                    emit(comp->curBC, OP_MOV, result, right, 0);
                } else {
                    jmp_pos = comp->curBC->count;
                    emit(comp->curBC, OP_JUMP_IF_TRUE, result, 0, 0);
                    int right = compile_expr(comp, expr->binary.right);
                    emit(comp->curBC, OP_MOV, result, right, 0);
                }
                int end = comp->curBC->count;
                comp->curBC->code[jmp_pos].r2 = end;
                release_to(comp, r_wm);  /* 鍙冲瓙鏍戜复鏃跺凡娑堣垂 */
                comp->last_temp = 1;
                return result;
            }

            /* 瀛楃涓插瓧闈㈤噺姣旇緝蹇€熻矾寰勶細EQK/NEQK锛堝父閲忕洿鎺ョ紪鐮侊紝锟?LOADK_STRING锟?*/
            if ((expr->binary.op == TOK_EQEQ || expr->binary.op == TOK_NEQ) &&
                expr->binary.left && expr->binary.right &&
                (expr->binary.left->type == EXPR_STRING || expr->binary.right->type == EXPR_STRING)) {
                Expr *lit = (expr->binary.right->type == EXPR_STRING) ? expr->binary.right : expr->binary.left;
                Expr *other = (lit == expr->binary.right) ? expr->binary.left : expr->binary.right;
                int lr = compile_expr(comp, other);
                int l_temp = comp->last_temp;
                char *buf = malloc((size_t)lit->stringVal.length + 1);
                memcpy(buf, lit->stringVal.start, (size_t)lit->stringVal.length); buf[lit->stringVal.length] = 0;
                int sidx = bytecode_add_string(comp->curBC, buf); free(buf);
                int result;
                if (l_temp) result = lr;
                else result = alloc_reg();
                emit(comp->curBC, (expr->binary.op == TOK_EQEQ) ? OP_EQK : OP_NEQK, result, lr, sidx);
                comp->last_temp = 1;
                return result;
            }

            if (expr->binary.op == TOK_IN) {
                int left = compile_expr(comp, expr->binary.left);
                int l_temp = comp->last_temp;
                int r_wm = next_register;
                int right = compile_expr(comp, expr->binary.right);
                int result = l_temp ? left : alloc_reg();
                emit(comp->curBC, OP_IN, result, left, right);
                if (l_temp) release_to(comp, r_wm);
                comp->last_temp = 1;
                return result;
            }

            OpCode op;
            switch (expr->binary.op) {
                case TOK_PLUS: {
                    /* L2: flatten left-assoc "+" chains (a+b+c+...) into one OP_CONCAT.
                       The VM folds left-to-right with identical per-step semantics to OP_ADD,
                       but strings get a single preallocated buffer instead of N reallocs. */
                    Expr *ops[70];
                    int nops = 0;
                    Expr *e = expr;
                    while (e && e->type == EXPR_BINARY && e->binary.op == TOK_PLUS) {
                        if (nops >= 68) { nops = 0; break; }  /* too long: fall back to plain OP_ADD */
                        ops[nops++] = e->binary.right;
                        e = e->binary.left;
                    }
                    if (nops > 0) ops[nops++] = e;  /* leftmost operand */
                    if (nops >= 3) {
                        /* OP_CONCAT assumes contiguous operand registers R[first..first+nops-1];
                           multi-register operands (INDEX/CALL/MEMBER/...) break contiguity -> fall back to OP_ADD */
                        for (int si = 0; si < nops; si++) {
                            if (ops[si]->type != EXPR_STRING && ops[si]->type != EXPR_NUMBER &&
                                ops[si]->type != EXPR_FLOAT && ops[si]->type != EXPR_BOOL && ops[si]->type != EXPR_IDENT) {
                                nops = 0;
                                break;
                            }
                        }
                    }
                    if (nops >= 3) {
                        /* reverse ops to evaluation order [leftmost .. rightmost] (also used by folding) */
                        for (int i = 0, j = nops - 1; i < j; i++, j--) {
                            Expr *t = ops[i]; ops[i] = ops[j]; ops[j] = t;
                        }
                        /* constant folding: pure string-literal chains collapse to a single constant */
                        int foldable = 1;
                        size_t flen = 0;
                        for (int fi = 0; fi < nops; fi++) {
                            if (ops[fi]->type != EXPR_STRING) { foldable = 0; break; }
                            for (int fk = 0; fk < ops[fi]->stringVal.length; fk++)
                                if (ops[fi]->stringVal.start[fk] == 92) { foldable = 0; break; }
                            if (!foldable) break;
                            flen += (size_t)ops[fi]->stringVal.length;
                        }
                        if (foldable) {
                            char *fbuf = malloc(flen + 1);
                            size_t foff = 0;
                            for (int fi = 0; fi < nops; fi++) {
                                memcpy(fbuf + foff, ops[fi]->stringVal.start, (size_t)ops[fi]->stringVal.length);
                                foff += (size_t)ops[fi]->stringVal.length;
                            }
                            fbuf[flen] = 0;
                            int fidx = bytecode_add_string(comp->curBC, fbuf);
                            free(fbuf);
                            int fr = alloc_reg();
                            emit(comp->curBC, OP_LOADK_STRING, fr, fidx, 0);
                            comp->last_temp = 1;                            return fr;
                        }
                        int first = -1, l_temp = 0, w_after_first = -1;
                        for (int i = 0; i < nops; i++) {
                            int r = compile_expr(comp, ops[i]);
                            if (i == 0) { first = r; l_temp = comp->last_temp; w_after_first = next_register; }
                        }
                        int result;
                        if (l_temp) {
                            result = first;  /* result reuses first temp operand register */
                            emit(comp->curBC, OP_CONCAT, result, first, nops);
                            release_to(comp, w_after_first);  /* operands 1..n-1 temps consumed */
                        } else {
                            result = alloc_reg();
                            emit(comp->curBC, OP_CONCAT, result, first, nops);
                            /* first operand is a persistent reg; caller releases the temps */
                        }
                        comp->last_temp = 1;
                        return result;
                    }
                    op = OP_ADD;
                    break;
                }

                case TOK_MINUS: op = OP_SUB; break;
                case TOK_STAR:  op = OP_MUL; break;
                case TOK_SLASH: op = OP_DIV; break;
                case TOK_PERCENT: op = OP_MOD; break;
                case TOK_EQEQ:  op = OP_EQ; break;
                case TOK_NEQ:   op = OP_NEQ; break;
                case TOK_LT:    op = OP_LT; break;
                case TOK_GT:    op = OP_GT; break;
                case TOK_LE:    op = OP_LE; break;
                case TOK_GE:    op = OP_GE; break;
                default: return -1;
            }
            /* 涓存椂鍙橀噺浼樺寲锛氱粨鏋滃鐢ㄥ乏鎿嶄綔鏁板瘎瀛樺櫒锛堣嫢宸︽槸涓存椂锛夛紝骞跺洖鏀跺彸瀛愭爲涓存椂瀵勫瓨锟?*/
            int left = compile_expr(comp, expr->binary.left);
            int l_temp = comp->last_temp;
            int r_wm = next_register;
            int right = compile_expr(comp, expr->binary.right);
            int result;
            if (l_temp) {
                result = left;
                emit(comp->curBC, op, result, left, right);
                release_to(comp, r_wm);
            } else {
                result = alloc_reg();
                emit(comp->curBC, op, result, left, right);
            }
            comp->last_temp = 1;
            return result;
        }
        case EXPR_CALL: {
            if (expr->call.callee->type == EXPR_IDENT) {
                char cn[256]; snprintf(cn, sizeof cn, "%.*s", (int)expr->call.callee->identName.length, expr->call.callee->identName.start);
                if (lookup_func(comp, cn) < 0 && (lookup_local(comp, cn) >= 0 || lookup_global_idx(comp, cn) >= 0)) {
                    int w0 = next_register; int callee = compile_expr(comp, expr->call.callee);
                    for (int i = 0; i < expr->call.argCount; i++) { int ar = compile_expr(comp, expr->call.args[i]); emit(comp->curBC, OP_PUSH_REG, ar, 0, 0); }
                    release_to(comp, w0); int out = alloc_reg(); emit(comp->curBC, OP_CALL_VALUE, callee, out, expr->call.argCount);
                    comp->last_temp = 1; return out;
                }
            }
            if (expr->call.callee->type == EXPR_IDENT || expr->call.callee->type == EXPR_MEMBER) {
                char fname[256];
                if (expr->call.callee->type == EXPR_IDENT) {
                    snprintf(fname, sizeof(fname), "%.*s", (int)expr->call.callee->identName.length,
                             expr->call.callee->identName.start);
                } else {
                    /* 鍛藉悕绌洪棿璋冪敤锛歶.add(...) -> 鎷兼帴锟?"u.add" */
            /* 鍛藉悕绌洪棿璋冪敤锛歛.b.c(...) 鈥斺€?ns_flatten 閾惧紡瑙ｆ瀽锛堟敮鎸佸祵濂楀懡鍚嶇┖闂达級 */
            char nsfull_c[512];
            if (ns_flatten(comp, expr->call.callee, nsfull_c, sizeof(nsfull_c))) {
                int w0 = next_register;
                snprintf(fname, sizeof(fname), "%s", nsfull_c);
                int fidx = lookup_func(comp, fname);   /* 鍐呴儴鍔?cur_ns 鍓嶇紑 */
                if (fidx >= 0) {
                    for (int i = 0; i < expr->call.argCount; i++) {
                        int arg_reg = compile_expr(comp, expr->call.args[i]);
                        emit(comp->curBC, OP_PUSH_REG, arg_reg, 0, 0);
                    }
                    release_to(comp, w0);
                    int result = alloc_reg();
                    emit(comp->curBC, OP_CALL_FUNC, fidx, result, expr->call.argCount);
                    comp->last_temp = 1;
                    return result;
                }
                /* ns 鍑芥暟鏈壘鍒帮細鍥炶惤 builtin锛堜繚鎸佹棫琛屼负锛?*/
                int name_idx = bytecode_add_string(comp->curBC, fname);
                for (int i = 0; i < expr->call.argCount; i++) {
                    int arg_reg = compile_expr(comp, expr->call.args[i]);
                    emit(comp->curBC, OP_PUSH_REG, arg_reg, 0, 0);
                }
                release_to(comp, w0);
                int result = alloc_reg();
                emit(comp->curBC, OP_CALL_BUILTIN, result, name_idx, expr->call.argCount);
                comp->last_temp = 1;
                return result;
            }
            /* 闈炲懡鍚嶇┖闂存垚鍛樿皟鐢紙match 鐗逛緥 / obj.member 鎷兼帴锛?*/
            Expr *objm = expr->call.callee->member.object;
            char memn[64];
            snprintf(memn, sizeof(memn), "%.*s", (int)expr->call.callee->member.member.length, expr->call.callee->member.member.start);
            if (strcmp(memn, "match") == 0) {
                int w0 = next_register;
                int oreg = compile_expr(comp, objm);
                emit(comp->curBC, OP_PUSH_REG, oreg, 0, 0);
                for (int i = 0; i < expr->call.argCount; i++) {
                    int arg_reg = compile_expr(comp, expr->call.args[i]);
                    emit(comp->curBC, OP_PUSH_REG, arg_reg, 0, 0);
                }
                release_to(comp, w0);
                int result = alloc_reg();
                int ni = bytecode_add_string(comp->curBC, "match");
                emit(comp->curBC, OP_CALL_BUILTIN, result, ni, 1 + expr->call.argCount);
                comp->last_temp = 1;
                return result;
            }
                    Expr *obj = expr->call.callee->member.object;
                    if (obj->type != EXPR_IDENT) return -1;
                    snprintf(fname, sizeof(fname), "%.*s.%.*s",
                             (int)obj->identName.length, obj->identName.start,
                             (int)expr->call.callee->member.member.length, expr->call.callee->member.member.start);
                }
                int w0 = next_register;
                /* 鑷畾涔夊嚱鏁颁紭鍏堬紙缂栬瘧鏈熺洿鎺ョ储寮曪紝杩愯鏃堕浂鏌ユ壘锟?*/
                int fidx = lookup_func(comp, fname);
                
                if (fidx >= 0) {
                    for (int i = 0; i < expr->call.argCount; i++) {
                        int arg_reg = compile_expr(comp, expr->call.args[i]);
                        emit(comp->curBC, OP_PUSH_REG, arg_reg, 0, 0);
                    }
                    release_to(comp, w0);  /* 鍙傛暟宸插帇鏍堬紝涓存椂瀵勫瓨鍣ㄥ彲鍥炴敹 */
                    int result = alloc_reg();
                    emit(comp->curBC, OP_CALL_FUNC, fidx, result, expr->call.argCount);
                    comp->last_temp = 1;
                    return result;
                }
                /* 鍐呯疆鍑芥暟鎸夊悕瀛楄皟鐢紙r2=瀛楃涓叉睜绱㈠紩锛夛紝閬垮厤杩愯鏃舵敞鍐岄『搴忓鑷寸殑绱㈠紩閿欎綅 */
                int name_idx = bytecode_add_string(comp->curBC, fname);
                /* 绮剧伒鏌ヨ鍑芥暟: ident 鍙傛暟浣滀负绮剧伒鍚嶅瓧绗︿覆甯搁噺 */
                int sprite_arg = -1;
                if (strcmp(fname, "x") == 0 || strcmp(fname, "y") == 0 ||
                    strcmp(fname, "vx") == 0 || strcmp(fname, "vy") == 0 ||
                    strcmp(fname, "touch") == 0 || strcmp(fname, "point_towards") == 0) sprite_arg = 0;
                for (int i = 0; i < expr->call.argCount; i++) {
                    int arg_reg;
                    if (i == sprite_arg && expr->call.args[i]->type == EXPR_IDENT) {
                        char sn[256];
                        snprintf(sn, sizeof(sn), "%.*s", (int)expr->call.args[i]->identName.length, expr->call.args[i]->identName.start);
                        arg_reg = alloc_reg();
                        emit(comp->curBC, OP_LOADK_STRING, arg_reg, bytecode_add_string(comp->curBC, sn), 0);
                    } else {
                        arg_reg = compile_expr(comp, expr->call.args[i]);
                    }
                    emit(comp->curBC, OP_PUSH_REG, arg_reg, 0, 0);
                }
                release_to(comp, w0);
                int result = alloc_reg();
                emit(comp->curBC, OP_CALL_BUILTIN, result, name_idx, expr->call.argCount);
                comp->last_temp = 1;
                return result;
            }
            return -1;
        }
        case EXPR_UNARY: {
            int operand = compile_expr(comp, expr->unary.operand);
            int o_temp = comp->last_temp;
            int r;
            if (o_temp && expr->unary.op != TOK_PLUS) {
                /* 鎿嶄綔鏁版槸涓存椂锛氬師鍦版眰鍊硷紝涓嶅啀鍒嗛厤鏂板瘎瀛樺櫒 */
                r = operand;
                if (expr->unary.op == TOK_NOT) emit(comp->curBC, OP_NOT, operand, operand, 0);
                else if (expr->unary.op == TOK_MINUS) emit(comp->curBC, OP_NEG, operand, operand, 0);
                else if (expr->unary.op == TOK_MIN) emit(comp->curBC, OP_MIN, operand, operand, 0);
                else if (expr->unary.op == TOK_MAX) emit(comp->curBC, OP_MAX, operand, operand, 0);
            } else {
                r = alloc_reg();
                if (expr->unary.op == TOK_NOT) emit(comp->curBC, OP_NOT, r, operand, 0);
                else if (expr->unary.op == TOK_MINUS) emit(comp->curBC, OP_NEG, r, operand, 0);
                else if (expr->unary.op == TOK_MIN) emit(comp->curBC, OP_MIN, r, operand, 0);
                else if (expr->unary.op == TOK_MAX) emit(comp->curBC, OP_MAX, r, operand, 0);
                else emit(comp->curBC, OP_MOV, r, operand, 0); /* 涓€锟?+ */
            }
            comp->last_temp = 1;
            return r;
        }
        case EXPR_LIST: {
            /* 姣忎釜鍏冪礌鍏堝帇鏍堬紝NEW_ARRAY 浠庢爤鏀堕泦锛堟敮鎸佸祵濂楁暟锟?浠绘剰琛ㄨ揪寮忥級 */
            int w0 = next_register;
            for (int i = 0; i < expr->list.count; i++) {
                int r = compile_expr(comp, expr->list.items[i]);
                emit(comp->curBC, OP_PUSH_REG, r, 0, 0);
            }
            release_to(comp, w0);  /* 鍏冪礌宸插帇鏍堬紝涓存椂瀵勫瓨鍣ㄥ彲鍥炴敹 */
            int r = alloc_reg();
            emit(comp->curBC, OP_NEW_ARRAY, r, 0, expr->list.count);
            comp->last_temp = 1;
            return r;
        }
        case EXPR_DICT: {
            /* 閿€煎浜ゆ浛鍘嬫爤锛孫P_NEW_DICT 浠庢爤锟?2*count 锟?*/
            int w0 = next_register;
            for (int i = 0; i < expr->dict.count * 2; i++) {
                int r = compile_expr(comp, expr->dict.items[i]);
                emit(comp->curBC, OP_PUSH_REG, r, 0, 0);
            }
            release_to(comp, w0);
            int r = alloc_reg();
            emit(comp->curBC, OP_NEW_DICT, r, 0, expr->dict.count);
            comp->last_temp = 1;
            return r;
        }
        case EXPR_INDEX: {
            int arr = compile_expr(comp, expr->index.object);
            int idx = compile_expr(comp, expr->index.index);
            int r = alloc_reg();
            emit(comp->curBC, OP_INDEX_GET, r, arr, idx);
            comp->last_temp = 1;
            return r;
        }
        case EXPR_MEMBER: {
            /* 鍛藉悕绌洪棿浼樺厛锛歛.b.c 涓?a 鈭?褰撳墠妯″潡鍙鍛藉悕绌洪棿 鈫?缂栬瘧鏈熻В鏋愪负甯﹀墠缂€鍏ㄥ眬
               锛堟斁鍦?meta 灞炴€т箣鍓嶏紝閬垮厤妯″潡鍏ㄥ眬鍚?type/range/int 绛夎鍔寔锛?*/
            char nsfull[512];
            if (ns_flatten(comp, expr, nsfull, sizeof(nsfull))) {
                int g = register_global(comp, nsfull);   /* 鍐呴儴鍔?cur_ns 鍓嶇紑 */
                int r = alloc_reg();
                emit(comp->curBC, OP_LOAD_GLOBAL, r, g, 0);
                comp->last_temp = 1;
                return r;
            }
            /* meta properties (any object): a.type / a.range / a.int / a.toint / ... */
            {
                char memN[128];
                snprintf(memN, sizeof(memN), "%.*s", (int)expr->member.member.length, expr->member.member.start);
                if (strcmp(memN, "range") == 0) {
                    /* range(value, gidx): gidx lets runtime return the be-bound set */
                    int g = -1;
                    if (expr->member.object->type == EXPR_IDENT) {
                        char objN[256];
                        snprintf(objN, sizeof(objN), "%.*s", (int)expr->member.object->identName.length, expr->member.object->identName.start);
                        g = register_global(comp, objN);
                    }
                    int a = compile_expr(comp, expr->member.object);
                    int w0 = next_register;
                    emit(comp->curBC, OP_PUSH_REG, a, 0, 0);
                    int gr = alloc_reg();
                    emit(comp->curBC, OP_LOADK_INT, gr, g, 0);
                    emit(comp->curBC, OP_PUSH_REG, gr, 0, 0);
                    release_to(comp, w0);
                    int result = alloc_reg();
                    int ni = bytecode_add_string(comp->curBC, "range");
                    emit(comp->curBC, OP_CALL_BUILTIN, result, ni, 2);
                    comp->last_temp = 1;
                    return result;
                }
                if (strcmp(memN, "type") == 0 || strcmp(memN, "int") == 0 ||
                    strcmp(memN, "float") == 0 || strcmp(memN, "str") == 0 || strcmp(memN, "bool") == 0) {
                    if (strcmp(memN, "int") == 0)
                        fprintf(stderr, "warning: x.int is deprecated, use int(x)\n");
                    int a = compile_expr(comp, expr->member.object);
                    int r = emit_builtin_call(comp, memN, a, 1);
                    comp->last_temp = 1;
                    return r;
                }
                if (expr->member.object->type == EXPR_IDENT) {
                    char objN2[256];
                    snprintf(objN2, sizeof(objN2), "%.*s", (int)expr->member.object->identName.length, expr->member.object->identName.start);
                    if (strcmp(memN, "toint") == 0 || strcmp(memN, "tofloat") == 0 ||
                        strcmp(memN, "tostr") == 0 || strcmp(memN, "tobool") == 0) {
                        if (strcmp(memN, "toint") == 0)
                            fprintf(stderr, "warning: x.toint is deprecated, use int(x)\n");
                        return compile_inplace_cast(comp, objN2, memN + 2);
                    }
                }
            }
            /* thread state props: worker.running / paused / stopped / finished */
            /* 锟?锟?^'worker.running / paused / stopped / finished */
            /* 绾跨▼鐘舵€佸睘鎬э細worker.running / paused / stopped / finished */
            if (expr->member.object->type == EXPR_IDENT) {
                char obj[256], mem[256];
                snprintf(obj, sizeof(obj), "%.*s", (int)expr->member.object->identName.length,
                         expr->member.object->identName.start);
                snprintf(mem, sizeof(mem), "%.*s", (int)expr->member.member.length, expr->member.member.start);
                int tidx = lookup_thread(comp, obj);
                if (tidx >= 0) {
                    int prop = -1;
                    if (strcmp(mem, "running") == 0) prop = 0;
                    else if (strcmp(mem, "paused") == 0) prop = 1;
                    else if (strcmp(mem, "stopped") == 0) prop = 2;
                    else if (strcmp(mem, "finished") == 0) prop = 3;
                    if (prop >= 0) {
                        int r = alloc_reg();
                        emit(comp->curBC, OP_THREAD_STATE, r, tidx, prop);
                        comp->last_temp = 1;
                        return r;
                    }
                    fprintf(stderr, "閿欒: 鏈煡绾跨▼灞炴€?'%s.%s'\n", obj, mem);
                    comp->last_temp = 1;
                    return alloc_reg();
                }
                /* 鍛藉悕绌洪棿鍙橀噺锛歶.count -> 鎷兼帴锟?"u.count" */
                char full[512];
                snprintf(full, sizeof(full), "%s.%s", obj, mem);
                int local = lookup_local(comp, full);
                if (local >= 0) { comp->last_temp = 0; return local; }
                int r = alloc_reg();
                int g_idx = register_global(comp, full)  /* namespace read: register (match assignment index) */;  /* 璇诲彇涓嶅垱寤哄叏灞€ */
                emit(comp->curBC, OP_LOAD_GLOBAL, r, g_idx, 0);
                comp->last_temp = 1;
                return r;
            }
            return -1;
        }
        case EXPR_LAMBDA: {
            char lname[64]; snprintf(lname, sizeof lname, "__lambda_%d", lambda_counter++);
            int fidx = register_func(comp, lname);
            Bytecode *saved_bc = comp->curBC; int saved_fn = comp->in_function;
            int saved_locals = comp->localCount, saved_gdecl = comp->gdeclCount;
            struct { char *name; int reg; } saved_local_table[1024];
            memcpy(saved_local_table, comp->locals, sizeof(saved_local_table));
            const char **saved_outer_names = lambda_outer_names; int saved_outer_count = lambda_outer_count;
            const char *outer_names[1024]; int outer_count = 0;
            for (int oi = 0; oi < saved_locals && oi < 1024; ++oi) outer_names[outer_count++] = comp->locals[oi].name;
            lambda_outer_names = outer_names; lambda_outer_count = outer_count;
            int saved_peak = comp->local_peak, saved_next = next_register, saved_reg_peak = reg_peak;
            Bytecode *fbc = malloc(sizeof(*fbc)); bytecode_init(fbc);
            comp->curBC = fbc; comp->in_function = 1; comp->localCount = 0; comp->gdeclCount = 0; comp->local_peak = 0; reset_regs();
            for (int i = 0; i < expr->lambda.paramCount; i++) { char pn[256]; snprintf(pn, sizeof pn, "%.*s", (int)expr->lambda.params[i].length, expr->lambda.params[i].start); alloc_local(comp, pn); }
            int rr = compile_expr(comp, expr->lambda.body); emit(fbc, OP_RETURN, rr, 0, 0); resolve_labels(comp);
            comp->mainBC->funcs[fidx] = fbc; comp->mainBC->func_argc[fidx] = expr->lambda.paramCount;
            comp->curBC = saved_bc; comp->in_function = saved_fn; memcpy(comp->locals, saved_local_table, sizeof(saved_local_table)); comp->localCount = saved_locals; comp->gdeclCount = saved_gdecl; comp->local_peak = saved_peak; next_register = saved_next; reg_peak = saved_reg_peak;
            lambda_outer_names = saved_outer_names; lambda_outer_count = saved_outer_count;
            int out = alloc_reg();
            for (int ci = 0; ci < fbc->capture_count; ++ci) {
                int cr = lookup_local(comp, fbc->capture_names[ci]);
                if (cr < 0) { fprintf(stderr, "[error] missing closure capture '%s'\n", fbc->capture_names[ci]); exit(1); }
                emit(comp->curBC, OP_PUSH_REG, cr, 0, 0);
            }
            emit(comp->curBC, OP_MAKE_FUNC, out, fidx, fbc->capture_count); comp->last_temp = 1; return out;
        }
        case EXPR_PROPAGATE: {
            int value = compile_expr(comp, expr->propagate.value);
            if (!comp->in_function) {
                return emit_builtin_call(comp, "unwrap", value, 1);
            }
            int ok = emit_builtin_call(comp, "is_ok", value, 1);
            int skip = comp->curBC->count; emit(comp->curBC, OP_JUMP_IF_TRUE, ok, 0, 0);
            emit(comp->curBC, OP_RETURN, value, 0, 0);
            comp->curBC->code[skip].r2 = comp->curBC->count;
            int unwrapped = emit_builtin_call(comp, "result_value", value, 1);
            comp->last_temp = 1; return unwrapped;
        }
        case EXPR_ARROW_CAST: {            /* in-place cast: a->int / a->float / a->str / a->bool (same as a.toXXX) */
            if (expr->arrowCast.object->type != EXPR_IDENT) {
                fprintf(stderr, "Error: '->' cast requires a variable\n");
                exit(1);
            }
            const char *cfn = expr->arrowCast.typeKind == 0 ? "int" :
                              expr->arrowCast.typeKind == 1 ? "float" :
                              expr->arrowCast.typeKind == 2 ? "str" : "bool";
            char oname[256];
            snprintf(oname, sizeof(oname), "%.*s", (int)expr->arrowCast.object->identName.length, expr->arrowCast.object->identName.start);
            int r = compile_inplace_cast(comp, oname, cfn);
            return r;
        }
        default: return -1;
    }
}

/* ---------- break 绠＄悊 ---------- */
static void add_break(int **list, int *count, int offset) {
    *list = realloc(*list, (*count + 1) * sizeof(int));
    (*list)[*count] = offset;
    (*count)++;
}

/* 閾惧紡绱㈠紩璧嬶拷?a[i][j]...[k] = v锛氫腑闂村眰锟?nil 鏃惰嚜鍔ㄥ垱寤烘暟锟?*/
static void compile_index_set_chain(Compiler *comp, Expr *target, Expr *value) {
    int depth = 0;
    Expr *cur = target;
    while (cur->type == EXPR_INDEX) { depth++; cur = cur->index.object; }
    Expr **idxs = malloc(depth * sizeof(Expr*));
    cur = target;
    for (int i = depth - 1; i >= 0; i--) { idxs[i] = cur->index.index; cur = cur->index.object; }

    int arr = compile_expr(comp, cur);
    for (int i = 0; i < depth - 1; i++) {
        int idx = compile_expr(comp, idxs[i]);
        int tmp = alloc_reg();
        emit(comp->curBC, OP_INDEX_GET, tmp, arr, idx);
        /* 浠呭綋涓棿灞備负 nil 鏃舵墠鍒涘缓鏂版暟缁勶紙閬垮厤瑕嗙洊宸叉湁 dict/鏁扮粍鍊硷級 */
        int isn = alloc_reg();
        emit(comp->curBC, OP_IS_NIL, isn, tmp, 0);
        int jf = comp->curBC->count;
        emit(comp->curBC, OP_JUMP_IF_FALSE, isn, 0, 0); /* 锟?nil 璺宠繃鍒涘缓 */
        emit(comp->curBC, OP_NEW_ARRAY, tmp, 0, 0);
        emit(comp->curBC, OP_INDEX_SET, arr, idx, tmp);
        comp->curBC->code[jf].r2 = comp->curBC->count;
        arr = tmp;
    }
    int last_idx = compile_expr(comp, idxs[depth - 1]);
    int val = compile_expr(comp, value);
    emit(comp->curBC, OP_INDEX_SET, arr, last_idx, val);
    free(idxs);
}

/* ---------- 璇彞缂栬瘧 ---------- */
static void compile_stmt(Compiler *comp, Stmt *stmt, int **break_list, int *break_count_ptr) {
    if (!stmt) return;
    switch (stmt->type) {
        case STMT_DECLARE: {
            /* resource declaration -> OP_DECLARE instructions (r1=kind, r2=float_pool idx) */
            for (int i = 0; i < stmt->declareStmt.count; i++) {
                int kind = 0;
                if (strcmp(stmt->declareStmt.keys[i], "mem") == 0) kind = 0;
                else if (strcmp(stmt->declareStmt.keys[i], "threads") == 0) kind = 1;
                else if (strcmp(stmt->declareStmt.keys[i], "time") == 0) kind = 2;
                else if (strcmp(stmt->declareStmt.keys[i], "inst") == 0) kind = 3;
                else if (strcmp(stmt->declareStmt.keys[i], "vram") == 0) kind = 4;
                else kind = 3; /* unknown -> inst */
                int fidx = bytecode_add_float(comp->curBC, stmt->declareStmt.values[i]);
                emit(comp->curBC, OP_DECLARE, kind, fidx, 0);
            }
            break;
        }
                case STMT_RECORD: {
            /* record default store = "both" */
            if (stmt->recordStmt.isDefault) {
                int store =0;
                if (stmt->recordStmt.value && stmt->recordStmt.value->type == EXPR_STRING) {
                    char vbuf[32];
                    int vl = stmt->recordStmt.value->stringVal.length <31 ? stmt->recordStmt.value->stringVal.length :31;
                    memcpy(vbuf, stmt->recordStmt.value->stringVal.start, vl);
                    vbuf[vl] = '\0';
                    if (strcmp(vbuf, "local") ==0) store =1;
                    else if (strcmp(vbuf, "server") ==0) store =2;
                    else if (strcmp(vbuf, "both") ==0) store =3;
                }
                emit(comp->curBC, OP_RECORD, -1, store, 0);
                break;
            }
            int g = register_global(comp, stmt->recordStmt.name);
            if (comp_record_is_reg(comp, g)) {
                int r = compile_expr(comp, stmt->recordStmt.value);
                emit(comp->curBC, OP_STORE_GLOBAL, g, r, 0);
                break;
            }
            RecordTag *tags = stmt->recordStmt.tags;
            int tagCount = stmt->recordStmt.tagCount;
            if (tagCount ==0 && comp->tag_depth >0) {
                tags = comp->tag_stack[comp->tag_depth -1];
                tagCount = comp->tag_stack_count[comp->tag_depth -1];
            }
            int meta = comp_record_meta(comp, tags, tagCount);
            int sidx = bytecode_add_string(comp->curBC, stmt->recordStmt.name);
            int r0 = compile_expr(comp, stmt->recordStmt.value);
            int rs = alloc_reg();
            emit(comp->curBC, OP_MOV, rs, r0, 0);
            emit(comp->curBC, OP_RECORD, g, (meta <<16) | sidx, rs);
            emit(comp->curBC, OP_STORE_GLOBAL, g, rs, 0);
            comp_record_mark(comp, g);
            break;
        }
        case STMT_TAG: {
        /* tag name item1, item2 -> CALL_BUILTIN "tag_register", name + items (pushed) */
        int r0 = alloc_reg();
        emit(comp->curBC, OP_LOADK_STRING, r0, bytecode_add_string(comp->curBC, stmt->tagStmt.name), 0);
        emit(comp->curBC, OP_PUSH_REG, r0, 0, 0);
        for (int i = 0; i < stmt->tagStmt.count; i++) {
            int r = alloc_reg();
            emit(comp->curBC, OP_LOADK_STRING, r, bytecode_add_string(comp->curBC, stmt->tagStmt.items[i]), 0);
            emit(comp->curBC, OP_PUSH_REG, r, 0, 0);
        }
        emit(comp->curBC, OP_CALL_BUILTIN, r0, bytecode_add_string(comp->curBC, "tag_register"), stmt->tagStmt.count + 1);
        break;
    }
    case STMT_CONST: {
    /* compile-time read-only global: register + assign once */
    int g = register_global(comp, stmt->constStmt.name);
    comp_const_mark(comp, g);
    int r = compile_expr(comp, stmt->constStmt.value);
    emit(comp->curBC, OP_STORE_GLOBAL, g, r, 0);
    break;
}

case STMT_WITH: {
            comp->tag_stack = realloc(comp->tag_stack, (comp->tag_depth +1) * sizeof(RecordTag*));
            comp->tag_stack_count = realloc(comp->tag_stack_count, (comp->tag_depth +1) * sizeof(int));
            comp->tag_stack[comp->tag_depth] = stmt->withStmt.tags;
            comp->tag_stack_count[comp->tag_depth] = stmt->withStmt.tagCount;
            comp->tag_depth++;
            for (int i =0; i < stmt->withStmt.bodyCount; i++)
                compile_stmt(comp, stmt->withStmt.body[i], break_list, break_count_ptr);
            comp->tag_depth--;
            break;
        }

        case STMT_CASE: {
            /* subject evaluated once; branches matched top-down; first hit runs then jumps out */
            int subj = compile_expr(comp, stmt->caseStmt.subject);
            protect_reg(comp, subj);
            int *end_jumps = NULL; int end_count = 0;
            for (int bi = 0; bi < stmt->caseStmt.branchCount; bi++) {
                CaseBranch *br = &stmt->caseStmt.branches[bi];
                int *body_jumps = NULL; int body_jcount = 0;
                int *skip_jumps = NULL; int skip_count = 0;
                int *guard_skips = NULL; int guard_skip_count = 0;
                int result_bind = -1;
                const char *result_field = NULL;
                if (stmt->caseStmt.isTry && br->mode == 0 && br->patternCount == 1) {
                    Expr *pattern = br->patterns[0];
                    int is_err = 0;
                    int is_result_branch = 0;
                    if (pattern->type == EXPR_IDENT) {
                        if (pattern->identName.length == 2 && strncmp(pattern->identName.start, "ok", 2) == 0) is_result_branch = 1;
                        if (pattern->identName.length == 3 && strncmp(pattern->identName.start, "err", 3) == 0) { is_result_branch = 1; is_err = 1; }
                    } else if (pattern->type == EXPR_CALL && pattern->call.callee && pattern->call.callee->type == EXPR_IDENT) {
                        if (pattern->call.callee->identName.length == 2 && strncmp(pattern->call.callee->identName.start, "ok", 2) == 0) is_result_branch = 1;
                        if (pattern->call.callee->identName.length == 3 && strncmp(pattern->call.callee->identName.start, "err", 3) == 0) { is_result_branch = 1; is_err = 1; }
                        if (is_result_branch && pattern->call.argCount == 1 && pattern->call.args[0]->type == EXPR_IDENT) {
                            char bn[256];
                            snprintf(bn, sizeof(bn), "%.*s", (int)pattern->call.args[0]->identName.length, pattern->call.args[0]->identName.start);
                            result_bind = register_global(comp, bn);
                            result_field = is_err ? "result_error" : "result_value";
                        }
                    }
                    if (is_result_branch) {
                        emit(comp->curBC, OP_PUSH_REG, subj, 0, 0);
                        int check = alloc_reg();
                        int bi = bytecode_add_string(comp->curBC, "is_ok");
                        emit(comp->curBC, OP_CALL_BUILTIN, check, bi, 1);
                        int cond = check;
                        if (is_err) { cond = alloc_reg(); emit(comp->curBC, OP_NOT, cond, check, 0); }

                        /* Result constructor patterns must constrain both the
                           variant and its payload.  A bare ok/err pattern
                           accepts any payload; ok(x)/err(x) binds it, while a
                           non-binding pattern (for example err("not_found"))
                           compares the extracted payload. */
                        int kind_false = comp->curBC->count;
                        emit(comp->curBC, OP_JUMP_IF_FALSE, cond, 0, 0);
                        add_break(&guard_skips, &guard_skip_count, kind_false);
                        if (pattern->type == EXPR_CALL && pattern->call.argCount == 1 &&
                            pattern->call.args[0]->type == EXPR_DICT) {
                            /* Structural payload pattern, e.g.
                               err({"kind": "not_found"}). */
                            int payload = alloc_reg();
                            emit(comp->curBC, OP_PUSH_REG, subj, 0, 0);
                            emit(comp->curBC, OP_CALL_BUILTIN, payload,
                                 bytecode_add_string(comp->curBC, is_err ? "result_error" : "result_value"), 1);
                            Expr *objpat = pattern->call.args[0];
                            for (int di = 0; di < objpat->dict.count; di++) {
                                int key = compile_expr(comp, objpat->dict.items[di * 2]);
                                emit(comp->curBC, OP_PUSH_REG, payload, 0, 0);
                                emit(comp->curBC, OP_PUSH_REG, key, 0, 0);
                                int has = alloc_reg();
                                emit(comp->curBC, OP_CALL_BUILTIN, has,
                                     bytecode_add_string(comp->curBC, "dict_has"), 2);
                                int jhas = comp->curBC->count;
                                emit(comp->curBC, OP_JUMP_IF_FALSE, has, 0, 0);
                                add_break(&guard_skips, &guard_skip_count, jhas);
                                int got = alloc_reg();
                                emit(comp->curBC, OP_INDEX_GET, got, payload, key);
                                Expr *field = objpat->dict.items[di * 2 + 1];
                                if (field->type == EXPR_IDENT &&
                                    !(field->identName.length == 1 && field->identName.start[0] == '_')) {
                                    char name[256];
                                    snprintf(name, sizeof(name), "%.*s", (int)field->identName.length, field->identName.start);
                                    emit(comp->curBC, OP_STORE_GLOBAL, register_global(comp, name), got, 0);
                                } else {
                                    int expected = compile_expr(comp, field);
                                    int same = alloc_reg();
                                    emit(comp->curBC, OP_EQ, same, got, expected);
                                    int field_false = comp->curBC->count;
                                    emit(comp->curBC, OP_JUMP_IF_FALSE, same, 0, 0);
                                    add_break(&guard_skips, &guard_skip_count, field_false);
                                }
                            }
                        } else if (pattern->type == EXPR_CALL && pattern->call.argCount == 1 &&
                                   pattern->call.args[0]->type != EXPR_IDENT) {
                            int actual = alloc_reg();
                            emit(comp->curBC, OP_PUSH_REG, subj, 0, 0);
                            emit(comp->curBC, OP_CALL_BUILTIN, actual,
                                 bytecode_add_string(comp->curBC, is_err ? "result_error" : "result_value"), 1);
                            int expected = compile_expr(comp, pattern->call.args[0]);
                            int same = alloc_reg();
                            emit(comp->curBC, OP_EQ, same, actual, expected);
                            int payload_false = comp->curBC->count;
                            emit(comp->curBC, OP_JUMP_IF_FALSE, same, 0, 0);
                            add_break(&guard_skips, &guard_skip_count, payload_false);
                        }
                        int jt = comp->curBC->count;
                        emit(comp->curBC, OP_JUMP, 0, 0, 0);
                        add_break(&body_jumps, &body_jcount, jt);
                    } else {
                        for (int pi = 0; pi < br->patternCount; pi++) {
                            int pat = compile_expr(comp, br->patterns[pi]);
                            int tmp = alloc_reg(); emit(comp->curBC, OP_EQ, tmp, subj, pat);
                            int jt = comp->curBC->count; emit(comp->curBC, OP_JUMP_IF_TRUE, tmp, 0, 0); add_break(&body_jumps, &body_jcount, jt);
                        }
                    }
                } else if (br->mode == 0) {
                    /* value list: any pattern == subject -> body; single list/set/comprehension literal -> in semantics */
                    if (br->patternCount == 1 && br->patterns[0]->type == EXPR_DICT) {
                        Expr *objpat = br->patterns[0];
                        for (int di = 0; di < objpat->dict.count; di++) {
                            int key = compile_expr(comp, objpat->dict.items[di * 2]);
                            emit(comp->curBC, OP_PUSH_REG, subj, 0, 0);
                            emit(comp->curBC, OP_PUSH_REG, key, 0, 0);
                            int has = alloc_reg();
                            emit(comp->curBC, OP_CALL_BUILTIN, has, bytecode_add_string(comp->curBC, "dict_has"), 2);
                            int jhas = comp->curBC->count;
                            emit(comp->curBC, OP_JUMP_IF_FALSE, has, 0, 0);
                            add_break(&guard_skips, &guard_skip_count, jhas);
                            int got = alloc_reg();
                            emit(comp->curBC, OP_INDEX_GET, got, subj, key);
                            Expr *field_pattern = objpat->dict.items[di * 2 + 1];
                            if (field_pattern->type == EXPR_DICT) {
                                for (int ni = 0; ni < field_pattern->dict.count; ni++) {
                                    int nkey = compile_expr(comp, field_pattern->dict.items[ni * 2]);
                                    emit(comp->curBC, OP_PUSH_REG, got, 0, 0);
                                    emit(comp->curBC, OP_PUSH_REG, nkey, 0, 0);
                                    int nhas = alloc_reg();
                                    emit(comp->curBC, OP_CALL_BUILTIN, nhas, bytecode_add_string(comp->curBC, "dict_has"), 2);
                                    int jnhas = comp->curBC->count;
                                    emit(comp->curBC, OP_JUMP_IF_FALSE, nhas, 0, 0);
                                    add_break(&guard_skips, &guard_skip_count, jnhas);
                                    int nGot = alloc_reg();
                                    emit(comp->curBC, OP_INDEX_GET, nGot, got, nkey);
                                    int nExpected = compile_expr(comp, field_pattern->dict.items[ni * 2 + 1]);
                                    int nEq = alloc_reg();
                                    emit(comp->curBC, OP_EQ, nEq, nGot, nExpected);
                                    int jn = comp->curBC->count;
                                    emit(comp->curBC, OP_JUMP_IF_FALSE, nEq, 0, 0);
                                    add_break(&guard_skips, &guard_skip_count, jn);
                                }
                                continue;
                            }
                            if (field_pattern->type == EXPR_IDENT &&
                                !(field_pattern->identName.length == 1 && field_pattern->identName.start[0] == '_')) {
                                char field_name[256];
                                snprintf(field_name, sizeof(field_name), "%.*s", (int)field_pattern->identName.length, field_pattern->identName.start);
                                emit(comp->curBC, OP_STORE_GLOBAL, register_global(comp, field_name), got, 0);
                                continue;
                            }
                            int expected = compile_expr(comp, field_pattern);
                            int eq = alloc_reg();
                            emit(comp->curBC, OP_EQ, eq, got, expected);
                            int jf = comp->curBC->count;
                            emit(comp->curBC, OP_JUMP_IF_FALSE, eq, 0, 0);
                            add_break(&guard_skips, &guard_skip_count, jf);
                        }
                        int jt = comp->curBC->count;
                        emit(comp->curBC, OP_JUMP, 0, 0, 0);
                        add_break(&body_jumps, &body_jcount, jt);
                    } else if (br->patternCount == 1 && br->patterns[0]->type == EXPR_IDENT &&
                        ((br->patterns[0]->identName.length == 1 && br->patterns[0]->identName.start[0] == '_') || br->guard)) {
                        int jt = comp->curBC->count;
                        emit(comp->curBC, OP_JUMP, 0, 0, 0);
                        add_break(&body_jumps, &body_jcount, jt);
                    } else if (br->patternCount == 1 && (br->patterns[0]->type == EXPR_LIST ||
                        br->patterns[0]->type == EXPR_SETLIT || br->patterns[0]->type == EXPR_SETCOMP)) {
                        int pat = compile_expr(comp, br->patterns[0]);
                        int tmp = alloc_reg();
                        emit(comp->curBC, OP_IN, tmp, subj, pat);
                        int jt = comp->curBC->count;
                        emit(comp->curBC, OP_JUMP_IF_TRUE, tmp, 0, 0);
                        add_break(&body_jumps, &body_jcount, jt);
                    } else {
                        for (int pi = 0; pi < br->patternCount; pi++) {
                            int pat = compile_expr(comp, br->patterns[pi]);
                            int tmp = alloc_reg();
                            emit(comp->curBC, OP_EQ, tmp, subj, pat);
                            int jt = comp->curBC->count;
                            emit(comp->curBC, OP_JUMP_IF_TRUE, tmp, 0, 0);
                            add_break(&body_jumps, &body_jcount, jt);
                        }
                    }
                } else if (br->mode == 1) {
                    /* comparison: subject cmpOp cmpExpr -> body */
                    int pat = compile_expr(comp, br->cmpExpr);
                    int tmp = alloc_reg();
                    OpCode cop;
                    switch (br->cmpOp) {
                        case TOK_LT:  cop = OP_LT;  break;
                        case TOK_GT:  cop = OP_GT;  break;
                        case TOK_LE:  cop = OP_LE;  break;
                        case TOK_GE:  cop = OP_GE;  break;
                        case TOK_NEQ: cop = OP_NEQ; break;
                        default:      cop = OP_EQ;  break; /* TOK_EQEQ */
                    }
                    emit(comp->curBC, cop, tmp, subj, pat);
                    int jt = comp->curBC->count;
                    emit(comp->curBC, OP_JUMP_IF_TRUE, tmp, 0, 0);
                    add_break(&body_jumps, &body_jcount, jt);
                } else if (br->mode == 3) {
                    /* membership / subset: subject in cmpExpr */
                    int pat = compile_expr(comp, br->cmpExpr);
                    int tmp = alloc_reg();
                    emit(comp->curBC, OP_IN, tmp, subj, pat);
                    int jt = comp->curBC->count;
                    emit(comp->curBC, OP_JUMP_IF_TRUE, tmp, 0, 0);
                    add_break(&body_jumps, &body_jcount, jt);
                } else if (br->mode == 4) {
                    /* regex match: match(subject, pattern) */
                    int w0 = next_register;
                    emit(comp->curBC, OP_PUSH_REG, subj, 0, 0);
                    int pat = compile_expr(comp, br->matchExpr);
                    emit(comp->curBC, OP_PUSH_REG, pat, 0, 0);
                    release_to(comp, w0);
                    int tmp = alloc_reg();
                    int ni = bytecode_add_string(comp->curBC, "match");
                    emit(comp->curBC, OP_CALL_BUILTIN, tmp, ni, 2);
                    int jt = comp->curBC->count;
                    emit(comp->curBC, OP_JUMP_IF_TRUE, tmp, 0, 0);
                    add_break(&body_jumps, &body_jcount, jt);
                }
                /* not matched: skip body (else branch has no skip) */
                int js;
                if (br->mode == 2) {
                    js = comp->curBC->count;
                } else {
                    js = comp->curBC->count;
                    emit(comp->curBC, OP_JUMP, 0, 0, 0);
                    add_break(&skip_jumps, &skip_count, js);
                }
                int body_start;
                int guard_true = -1;
                if (br->guard) {
                    int guard_start = comp->curBC->count;
                    for (int ji = 0; ji < body_jcount; ji++) comp->curBC->code[body_jumps[ji]].r2 = guard_start;
                    if (result_bind >= 0 && result_field) {
                        int value = alloc_reg();
                        emit(comp->curBC, OP_PUSH_REG, subj, 0, 0);
                        emit(comp->curBC, OP_CALL_BUILTIN, value, bytecode_add_string(comp->curBC, result_field), 1);
                        emit(comp->curBC, OP_STORE_GLOBAL, result_bind, value, 0);
                    }
                    if (br->patternCount == 1 && br->patterns[0]->type == EXPR_IDENT) {
                        char gn[256];
                        snprintf(gn, sizeof(gn), "%.*s", (int)br->patterns[0]->identName.length, br->patterns[0]->identName.start);
                        int gg = register_global(comp, gn);
                        emit(comp->curBC, OP_STORE_GLOBAL, gg, subj, 0);
                    }
                    int gr = compile_expr(comp, br->guard);
                    guard_true = comp->curBC->count;
                    emit(comp->curBC, OP_JUMP_IF_TRUE, gr, 0, 0);
                    int guard_false = comp->curBC->count;
                    emit(comp->curBC, OP_JUMP, 0, 0, 0);
                    add_break(&guard_skips, &guard_skip_count, guard_false);
                    body_start = comp->curBC->count;
                } else {
                    body_start = comp->curBC->count;
                }
                if (br->guard && guard_true >= 0) comp->curBC->code[guard_true].r2 = body_start;
                if (!br->guard && result_bind >= 0 && result_field) {
                    int value = alloc_reg();
                    emit(comp->curBC, OP_PUSH_REG, subj, 0, 0);
                    emit(comp->curBC, OP_CALL_BUILTIN, value, bytecode_add_string(comp->curBC, result_field), 1);
                    emit(comp->curBC, OP_STORE_GLOBAL, result_bind, value, 0);
                }
                for (int i = 0; i < br->bodyCount; i++)
                    compile_stmt(comp, br->body[i], break_list, break_count_ptr);
                int je = comp->curBC->count;
                emit(comp->curBC, OP_JUMP, 0, 0, 0);
                add_break(&end_jumps, &end_count, je);
                if (!br->guard) {
                    for (int i = 0; i < body_jcount; i++)
                        comp->curBC->code[body_jumps[i]].r2 = body_start;
                }
                for (int i = 0; i < skip_count; i++)
                    comp->curBC->code[skip_jumps[i]].r2 = comp->curBC->count; /* next branch */
                for (int i = 0; i < guard_skip_count; i++)
                    comp->curBC->code[guard_skips[i]].r2 = comp->curBC->count;
                free(body_jumps); free(skip_jumps); free(guard_skips);
                release_temps(comp);
            }
            for (int i = 0; i < end_count; i++)
                comp->curBC->code[end_jumps[i]].r2 = comp->curBC->count; /* case end */
            free(end_jumps);
            release_temps(comp);
            break;
        }
        case STMT_USING: {
            if (stmt->usingStmt.modName.start && stmt->usingStmt.modName.length > 0) {
                char mod_name[256];
                snprintf(mod_name, sizeof(mod_name), "%.*s",
                         (int)stmt->usingStmt.modName.length, stmt->usingStmt.modName.start);
                int dup = 0;
                for (int i = 0; i < comp->usingCount; i++)
                    if (strcmp(comp->usingMods[i], mod_name) == 0) { dup = 1; break; }
                if (!dup && strcmp(mod_name, "thread") != 0) {
                    comp->usingMods = realloc(comp->usingMods, (comp->usingCount + 1) * sizeof(char*));
                    comp->usingMods[comp->usingCount++] = strdup(mod_name);
                }
            }
            break;
        }
        case STMT_SHOW: case STMT_HIDE:
        case STMT_NEW: case STMT_CURSOR: case STMT_DELETE:
        case STMT_BLOCK_DEF: case STMT_THREAD_DEF: case STMT_ON:
            break;

        case STMT_IMPORT:
        case STMT_INCLUDE: {
            /* 缂栬瘧鏈熼€掑綊瑙ｆ瀽瀛愭枃浠讹細鍐呰仈缂栬瘧瀛愭ā鍧楄鍙ワ紙鏂囨湰椤哄簭璇箟锛夈€?
               绗竴閬?collect_decls 宸?parse 骞舵敹闆嗭紙imports[] 鏉＄洰锛夛紝杩欓噷澶嶇敤鍏?Program銆?*/
            char rel[512], ns[256], key[1100];
            snprintf(rel, sizeof(rel), "%.*s", (int)stmt->importStmt.path.length, stmt->importStmt.path.start);
            ns[0] = 0;
            int has_ns = (stmt->type == STMT_IMPORT && stmt->importStmt.ns.length > 0);
            if (has_ns) snprintf(ns, sizeof(ns), "%.*s", (int)stmt->importStmt.ns.length, stmt->importStmt.ns.start);
            char *full = resolve_import_path(comp->cur_dir, rel);
            snprintf(key, sizeof(key), "%s\x01%s\x01%s", full, has_ns ? ns : "", comp->cur_ns);
            int found = -1;
            for (int k = 0; k < comp->import_count; k++)
                if (strcmp(comp->imports[k].key, key) == 0) { found = k; break; }
            if (found < 0) { free(full); break; }          /* 鏈敹闆嗭紙寮傚父锛屽拷鐣ワ級 */
            if (comp->imports[found].state == 2) { free(full); break; }  /* 鑿卞舰渚濊禆锛氬凡缂栬瘧 */
            if (comp->imports[found].state != 1) { free(full); break; }
            comp->imports[found].state = 2;
            Program *sub = comp->imports[found].prog;
            char saved_ns[512], saved_dir[1024];
            strcpy(saved_ns, comp->cur_ns);
            strcpy(saved_dir, comp->cur_dir);
            int saved_visible = comp->ns_visible_count;
            comp->import_depth++;
            if (has_ns) {
                strncat(comp->cur_ns, ns, sizeof(comp->cur_ns) - strlen(comp->cur_ns) - 1);
                strncat(comp->cur_ns, ".", sizeof(comp->cur_ns) - strlen(comp->cur_ns) - 1);
                comp->ns_visible_count = 0;   /* 瀛愭ā鍧楃嫭绔嬪彲瑙佸懡鍚嶇┖闂撮泦 */
            }
            dir_of(full, comp->cur_dir, sizeof(comp->cur_dir));
            int *breaks = NULL; int bcount = 0;
            for (int i = 0; i < sub->count; i++)
                compile_stmt(comp, sub->stmts[i], &breaks, &bcount);
            free(breaks);
            comp->import_depth--;
            strcpy(comp->cur_ns, saved_ns);
            strcpy(comp->cur_dir, saved_dir);
            comp->ns_visible_count = saved_visible;
            if (has_ns) {
                /* import 瀹屾垚锛歯s 瀵瑰綋鍓嶆ā鍧楀彲瑙侊紙m.x / m.f() 鍙В鏋愶級 */
                if (comp->ns_visible_count >= comp->ns_visible_cap) {
                    comp->ns_visible_cap = comp->ns_visible_cap == 0 ? 8 : comp->ns_visible_cap * 2;
                    comp->ns_visible = realloc(comp->ns_visible, comp->ns_visible_cap * sizeof(char*));
                }
                comp->ns_visible[comp->ns_visible_count++] = strdup(ns);
            }
            free(full);
            break;
        }

        case STMT_GUI: {
            char verb[64];
            snprintf(verb, sizeof(verb), "%.*s", (int)stmt->guiStmt.verb.length, stmt->guiStmt.verb.start);
            if (strcmp(verb, "forever") == 0) {
                int start = comp->curBC->count;
                int cond = alloc_reg();
                emit(comp->curBC, OP_LOADK_BOOL, cond, 1, 0);
                int jf = comp->curBC->count;
                emit(comp->curBC, OP_JUMP_IF_FALSE, cond, 0, 0);
                int *breaks = NULL; int bcount = 0;
                for (int i = 0; i < stmt->guiStmt.bodyCount; i++)
                    compile_stmt(comp, stmt->guiStmt.body[i], &breaks, &bcount);
                emit(comp->curBC, OP_JUMP, 0, start, 0);
                int end = comp->curBC->count;
                comp->curBC->code[jf].r2 = end;
                /* fix: bare break inside forever must jump to loop end,
                   not offset 0 (was unpatched -> ip reset to 0) */
                for (int k = 0; k < bcount; k++)
                    comp->curBC->code[breaks[k]].r2 = end;
                free(breaks);
                release_temps(comp);
                break;
            }
            if (strcmp(verb, "when") == 0) {
                if (stmt->guiStmt.argCount >= 1 && stmt->guiStmt.args[0]->type == EXPR_IDENT) {
                    /* when flag { } 锟?涓昏剼锟? body 缂栬瘧杩涘綋鍓嶄綔鐢ㄥ煙 */
                    int *breaks = NULL; int bcount = 0;
                    for (int i = 0; i < stmt->guiStmt.bodyCount; i++)
                        compile_stmt(comp, stmt->guiStmt.body[i], &breaks, &bcount);
                    free(breaks);
                } else {
                    /* when "msg" { } 锟?骞挎挱绾跨▼: 寰幆绛夊緟骞挎挱鍚庢墽锟?body */
                    char tname[256];
                    snprintf(tname, sizeof(tname), "when#%.*s", (int)stmt->guiStmt.args[0]->stringVal.length, stmt->guiStmt.args[0]->stringVal.start);
                    int tidx = register_thread(comp, tname);
                    if (tidx >= 0) {
                        Bytecode *tbc = malloc(sizeof(Bytecode)); bytecode_init(tbc);
                        Bytecode *saved = comp->curBC;
                        comp->curBC = tbc;
                        comp->in_function = 1; comp->in_thread = 1;
                        comp->localCount = 0; comp->gdeclCount = 0; comp->local_peak = 0;
                        reset_regs();
                        int loop = tbc->count;
                        int r0 = alloc_reg();
                        emit(tbc, OP_LOADK_STRING, r0, bytecode_add_string(tbc, (char*)stmt->guiStmt.args[0]->stringVal.start), 0);
                        emit(tbc, OP_PUSH_REG, r0, 0, 0);
                        int r1 = alloc_reg();
                        emit(tbc, OP_CALL_BUILTIN, r1, bytecode_add_string(tbc, "gui_wait_broadcast"), 1);
                        int *breaks = NULL; int bcount = 0;
                        for (int i = 0; i < stmt->guiStmt.bodyCount; i++)
                            compile_stmt(comp, stmt->guiStmt.body[i], &breaks, &bcount);
                        free(breaks);
                        emit(tbc, OP_JUMP, 0, loop, 0);
                        emit(tbc, OP_HALT, 0, 0, 0);
                        comp->curBC = saved;
                        comp->in_function = 0; comp->in_thread = 0;
                        comp->localCount = 0;
                        comp->mainBC->threads[tidx] = tbc;
                        comp->mainBC->thread_argc[tidx] = 0;
                    }
                }
                break;
            }
            if (strcmp(verb, "on") == 0) {
                if (stmt->guiStmt.argCount >= 1 && stmt->guiStmt.args[0]->type == EXPR_IDENT) {
                    char sname[256];
                    snprintf(sname, sizeof(sname), "%.*s", (int)stmt->guiStmt.args[0]->identName.length, stmt->guiStmt.args[0]->identName.start);
                    char tname[256];
                    snprintf(tname, sizeof(tname), "sprite#%s", sname);
                    int tidx = register_thread(comp, tname);
                    if (tidx >= 0) {
                        /* 涓荤▼搴忓惎鍔ㄧ簿鐏电嚎锟?*/
                        emit(comp->curBC, OP_THREAD_START, tidx, alloc_reg(), 0);
                        Bytecode *tbc = malloc(sizeof(Bytecode)); bytecode_init(tbc);
                        Bytecode *saved = comp->curBC;
                        comp->curBC = tbc;
                        comp->in_function = 1; comp->in_thread = 1;
                        comp->localCount = 0; comp->gdeclCount = 0; comp->local_peak = 0;
                        reset_regs();
                        /* 绾跨▼浣撳紑锟? gui_bind(绮剧伒锟? */
                        int r0 = alloc_reg();
                        emit(tbc, OP_LOADK_STRING, r0, bytecode_add_string(tbc, sname), 0);
                        emit(tbc, OP_PUSH_REG, r0, 0, 0);
                        int r1 = alloc_reg();
                        emit(tbc, OP_CALL_BUILTIN, r1, bytecode_add_string(tbc, "gui_bind"), 1);
                        int *breaks = NULL; int bcount = 0;
                        for (int i = 0; i < stmt->guiStmt.bodyCount; i++)
                            compile_stmt(comp, stmt->guiStmt.body[i], &breaks, &bcount);
                        free(breaks);
                        emit(tbc, OP_HALT, 0, 0, 0);
                        comp->curBC = saved;
                        comp->in_function = 0; comp->in_thread = 0;
                        comp->localCount = 0;
                        comp->mainBC->threads[tidx] = tbc;
                        comp->mainBC->thread_argc[tidx] = 0;
                    }
                }
                break;
            }
            /* 鍏朵粬 verb 锟?CALL_BUILTIN "gui_<verb>", args */
            {
                /* 绮剧伒鍚嶅弬鏁颁綅锟?ident 缂栬瘧涓哄瓧绗︿覆甯搁噺) */
                int sip = -1;
                int argc = stmt->guiStmt.argCount;
                if (strcmp(verb, "sprite") == 0) sip = 0;
                else if (strcmp(verb, "goto") == 0) { if (argc >= 3) sip = 0; }
                else if (strcmp(verb, "move") == 0 || strcmp(verb, "costume") == 0 ||
                         strcmp(verb, "face") == 0 || strcmp(verb, "turn") == 0 ||
                         strcmp(verb, "point_to") == 0 || strcmp(verb, "gravity") == 0 ||
                         strcmp(verb, "size") == 0 || strcmp(verb, "bounce") == 0 ||
                         strcmp(verb, "sound") == 0 || strcmp(verb, "music") == 0 ||
                         strcmp(verb, "fixed") == 0 || strcmp(verb, "ghost") == 0 ||
                         strcmp(verb, "clickable") == 0 || strcmp(verb, "drag") == 0 ||
                         strcmp(verb, "secret") == 0) { if (argc >= 2) sip = 0; }
                else if (strcmp(verb, "show") == 0 || strcmp(verb, "hide") == 0) { if (argc >= 1) sip = 0; }
                else if (strcmp(verb, "box") == 0) { if (argc >= 5) sip = 0; }
                else if (strcmp(verb, "velocity") == 0) { if (argc >= 3) sip = 0; }
                char bname[256];
                snprintf(bname, sizeof(bname), "gui_%s", verb);
                int bidx = bytecode_add_string(comp->curBC, bname);
                for (int i = 0; i < argc; i++) {
                    int r;
                    if (i == sip && stmt->guiStmt.args[i]->type == EXPR_IDENT) {
                        char sn[256];
                        snprintf(sn, sizeof(sn), "%.*s", (int)stmt->guiStmt.args[i]->identName.length, stmt->guiStmt.args[i]->identName.start);
                        r = alloc_reg();
                        emit(comp->curBC, OP_LOADK_STRING, r, bytecode_add_string(comp->curBC, sn), 0);
                    } else {
                        r = compile_expr(comp, stmt->guiStmt.args[i]);
                    }
                    emit(comp->curBC, OP_PUSH_REG, r, 0, 0);
                }
                int res = alloc_reg();
                emit(comp->curBC, OP_CALL_BUILTIN, res, bidx, argc);
                release_temps(comp);
            }
            break;
        }

        case STMT_GLOBAL: {
            /* 浠呰褰曞０鏄庯紙鍑芥暟鍐呭啓鍏ㄥ眬鐨勬樉寮忔巿鏉冿級锛屼笉鍙戝皠鎸囦护锛涘悕瀛楀甫鍛藉悕绌洪棿鍓嶇紑 */
            for (int i = 0; i < stmt->globalStmt.nameCount; i++) {
                char gname[256], fname[512];
                snprintf(gname, sizeof(gname), "%.*s", (int)stmt->globalStmt.names[i].length, stmt->globalStmt.names[i].start);
                ns_full(comp, gname, fname, sizeof(fname));
                if (lookup_global_decl(comp, gname) < 0) {
                    if (comp->gdeclCount >= 1024) {
                        fprintf(stderr, "[error] global-declaration limit 1024 exceeded ('%s').\n", fname);
                        exit(1);
                    }
                    comp->gdecl[comp->gdeclCount++] = strdup(fname);
                }
            }
            break;
        }
        case STMT_MAIN: {
            comp->mainBC->main_flags = stmt->mainStmt.flags;
            /* main{} 锟?body 灞曞紑缂栬瘧杩涗富绋嬪簭锛堜富绾跨▼鍏ュ彛锟?*/
            int *breaks = NULL; int bcount = 0;
            for (int i = 0; i < stmt->mainStmt.bodyCount; i++)
                compile_stmt(comp, stmt->mainStmt.body[i], &breaks, &bcount);
            free(breaks);
            break;
        }
        case STMT_START: {
            /* 鍙傛暟鍘嬫爤锛堜笌鍑芥暟璋冪敤涓€鑷达級 */
            for (int i = 0; i < stmt->startStmt.argCount; i++) {
                int r = compile_expr(comp, stmt->startStmt.args[i]);
                emit(comp->curBC, OP_PUSH_REG, r, 0, 0);
            }
            char tname[256];
            snprintf(tname, sizeof(tname), "%.*s", (int)stmt->startStmt.name.length, stmt->startStmt.name.start);
            int tidx = lookup_thread(comp, tname);
            if (tidx < 0) { fprintf(stderr, "閿欒: 鏈畾涔夌殑绾跨▼ '%s'\n", tname); break; }
            int result = alloc_reg();
            emit(comp->curBC, OP_THREAD_START, tidx, result, stmt->startStmt.argCount);
            release_temps(comp);
            break;
        }
        case STMT_THREAD_CTRL: {
            char tname[256];
            snprintf(tname, sizeof(tname), "%.*s", (int)stmt->threadCtrlStmt.name.length, stmt->threadCtrlStmt.name.start);
            int tidx;
            if (strcmp(tname, "this") == 0) tidx = -2;  /* 褰撳墠绾跨▼ */
            else {
                tidx = lookup_thread(comp, tname);
                if (tidx < 0) { fprintf(stderr, "閿欒: 鏈畾涔夌殑绾跨▼ '%s'\n", tname); break; }
            }
            emit(comp->curBC, OP_THREAD_CTRL, tidx, stmt->threadCtrlStmt.op, 0);
            break;
        }
        case STMT_JOIN: {
            char tname[256];
            snprintf(tname, sizeof(tname), "%.*s", (int)stmt->joinStmt.name.length, stmt->joinStmt.name.start);
            int tidx = lookup_thread(comp, tname);
            if (tidx < 0) { fprintf(stderr, "閿欒: 鏈畾涔夌殑绾跨▼ '%s'\n", tname); break; }
            int timeout_reg = -1;
            if (stmt->joinStmt.timeout) timeout_reg = compile_expr(comp, stmt->joinStmt.timeout);
            emit(comp->curBC, OP_THREAD_JOIN, tidx, timeout_reg, 0);
            release_temps(comp);
            break;
        }
        case STMT_THREAD_WAIT: {
            char tname[256];
            snprintf(tname, sizeof(tname), "%.*s", (int)stmt->threadWaitStmt.name.length, stmt->threadWaitStmt.name.start);
            int tidx = lookup_thread(comp, tname);
            if (tidx < 0) { fprintf(stderr, "閿欒: 鏈畾涔夌殑绾跨▼ '%s'\n", tname); break; }
            if (stmt->threadWaitStmt.mode == 0) {
                /* worker.wait N锛氭殏鍋滅洰鏍囩嚎锟?N 绉掞紙VM 瀹氭椂鎭㈠锟?*/
                int sec_reg = compile_expr(comp, stmt->threadWaitStmt.arg);
                emit(comp->curBC, OP_THREAD_WAIT, tidx, sec_reg, 0);
            } else {
                /* worker.wait until cond[, timeout]锛氱紪璇戜负杞寰幆锛屾潯浠朵负鐪熸垨瓒呮椂锟?resume */
                int jout = -1;
                int t_reg = -1;
                if (stmt->threadWaitStmt.timeout) {
                    int base = compile_expr(comp, stmt->threadWaitStmt.timeout);
                    t_reg = alloc_reg();
                    int hundred = alloc_reg(); emit(comp->curBC, OP_LOADK_INT, hundred, 100, 0);
                    emit(comp->curBC, OP_MUL, t_reg, base, hundred);  /* 杩戜技 100 锟?锟?*/
                }
                int loop = comp->curBC->count;
                int cond_r = compile_expr(comp, stmt->threadWaitStmt.arg);
                int jtrue = comp->curBC->count;
                emit(comp->curBC, OP_JUMP_IF_TRUE, cond_r, 0, 0);
                if (t_reg >= 0) {
                    int zero = alloc_reg(); emit(comp->curBC, OP_LOADK_INT, zero, 0, 0);
                    int cmp = alloc_reg(); emit(comp->curBC, OP_LE, cmp, t_reg, zero);
                    jout = comp->curBC->count;
                    emit(comp->curBC, OP_JUMP_IF_TRUE, cmp, 0, 0);
                    int one = alloc_reg(); emit(comp->curBC, OP_LOADK_INT, one, 1, 0);
                    emit(comp->curBC, OP_SUB, t_reg, t_reg, one);
                }
                int f = alloc_reg();
                int fidx = bytecode_add_float(comp->curBC, 0.01);
                emit(comp->curBC, OP_LOADK_FLOAT, f, fidx, 0);
                emit(comp->curBC, OP_WAIT, f, 0, 0);
                emit(comp->curBC, OP_JUMP, 0, loop, 0);
                int wake = comp->curBC->count;
                comp->curBC->code[jtrue].r2 = wake;
                if (jout >= 0) comp->curBC->code[jout].r2 = wake;
                emit(comp->curBC, OP_THREAD_CTRL, tidx, THREAD_OP_RESUME, 0);
            }
            break;
        }
        case STMT_LOCK: {
            char mname[256];
            snprintf(mname, sizeof(mname), "%.*s", (int)stmt->lockStmt.name.length, stmt->lockStmt.name.start);
            int midx = register_mutex(comp, mname);
            if (stmt->lockStmt.isBlock == 2) {
                emit(comp->curBC, OP_LOCK, midx, 1, 0);
            } else if (stmt->lockStmt.isBlock == 1) {
                emit(comp->curBC, OP_LOCK, midx, 0, 0);
                int *breaks = NULL; int bcount = 0;
                for (int i = 0; i < stmt->lockStmt.bodyCount; i++)
                    compile_stmt(comp, stmt->lockStmt.body[i], &breaks, &bcount);
                free(breaks);
                emit(comp->curBC, OP_LOCK, midx, 1, 0);
            } else {
                emit(comp->curBC, OP_LOCK, midx, 0, 0);
            }
            break;
        }
        case STMT_SEND: {
            char tname[256];
            snprintf(tname, sizeof(tname), "%.*s", (int)stmt->sendStmt.name.length, stmt->sendStmt.name.start);
            int tidx = lookup_thread(comp, tname);
            if (tidx < 0) { fprintf(stderr, "閿欒: 鏈畾涔夌殑绾跨▼ '%s'\n", tname); break; }
            int r = compile_expr(comp, stmt->sendStmt.msg);
            emit(comp->curBC, OP_SEND, tidx, r, 0);
            release_temps(comp);
            break;
        }
        case STMT_RECV: {
            int timeout_reg = -1;
            if (stmt->recvStmt.timeout) timeout_reg = compile_expr(comp, stmt->recvStmt.timeout);
            int result = alloc_reg();
            emit(comp->curBC, OP_RECV, result, timeout_reg, 0);
            if (stmt->recvStmt.target && stmt->recvStmt.target->type == EXPR_IDENT) {
                char vname[256];
                snprintf(vname, sizeof(vname), "%.*s", (int)stmt->recvStmt.target->identName.length,
                         stmt->recvStmt.target->identName.start);
                int local = lookup_local(comp, vname);
                if (local >= 0) emit(comp->curBC, OP_MOV, local, result, 0);
                else { int g = register_global(comp, vname); emit(comp->curBC, OP_STORE_GLOBAL, g, result, 0); }
            }
            release_temps(comp);
            break;
        }

        case STMT_WINDOW: {
            // window(width, height, title) -> 缂栬瘧涓哄唴缃嚱鏁拌皟锟?
            if (stmt->windowStmt.width) compile_expr(comp, stmt->windowStmt.width);
            else { int r = alloc_reg(); emit(comp->curBC, OP_LOADK_INT, r, 800, 0); }
            if (stmt->windowStmt.height) compile_expr(comp, stmt->windowStmt.height);
            else { int r = alloc_reg(); emit(comp->curBC, OP_LOADK_INT, r, 600, 0); }
            if (stmt->windowStmt.title) compile_expr(comp, stmt->windowStmt.title);
            else { int r = alloc_reg(); int idx = bytecode_add_string(comp->curBC, "Inimerse"); emit(comp->curBC, OP_LOADK_STRING, r, idx, 0); }

            int builtin_idx = lookup_builtin(comp, "window");
            if (builtin_idx >= 0) {
                emit(comp->curBC, OP_PUSH_REG, alloc_reg() - 3, 0, 0); // 鏍囬瀵勫瓨锟?
                emit(comp->curBC, OP_PUSH_REG, alloc_reg() - 2, 0, 0); // 楂樺害瀵勫瓨锟?
                emit(comp->curBC, OP_PUSH_REG, alloc_reg() - 1, 0, 0); // 瀹藉害瀵勫瓨锟?
                int result = alloc_reg();
                emit(comp->curBC, OP_CALL_BUILTIN, result, builtin_idx, 3);
            }
            release_temps(comp);
            break;
        }

        case STMT_SAY: {
            int r = compile_expr(comp, stmt->sayStmt.message);
            if (comp->in_thread) {
                /* 绮剧伒绾跨▼锟? say 锟?姘旀场(褰撳墠缁戝畾绮剧伒) */
                emit(comp->curBC, OP_PUSH_REG, r, 0, 0);
                int rr = alloc_reg();
                emit(comp->curBC, OP_CALL_BUILTIN, rr, bytecode_add_string(comp->curBC, "gui_say"), 1);
            } else {
                emit(comp->curBC, OP_SAY, r, 0, 0);
            }
            release_temps(comp);
            break;
        }
        case STMT_WAIT: {
            int r = compile_expr(comp, stmt->waitStmt.duration);
            emit(comp->curBC, OP_WAIT, r, 0, 0);
            release_temps(comp);
            break;
        }
        case STMT_STOP:
            emit(comp->curBC, OP_STOP, 0, 0, 0);
            break;
        case STMT_YIELD:
            emit(comp->curBC, OP_YIELD, 0, 0, 0);
            break;

        case STMT_TYPE: {
            char tname[256];
            snprintf(tname, sizeof(tname), "%.*s", (int)stmt->typeStmt.name.length, stmt->typeStmt.name.start);
            int g = register_global(comp, tname);
            int setReg = compile_expr(comp, stmt->typeStmt.set);
            emit(comp->curBC, OP_STORE_GLOBAL, g, setReg, 0);
            release_temps(comp);
            break;
        }
        case STMT_BE: {
            char bname[256];
            snprintf(bname, sizeof(bname), "%.*s", (int)stmt->beStmt.name.length, stmt->beStmt.name.start);
            int g = register_global(comp, bname);
            int setReg = compile_expr(comp, stmt->beStmt.set);
            int initReg = -1;
            if (stmt->beStmt.init) initReg = compile_expr(comp, stmt->beStmt.init);
            emit(comp->curBC, OP_BE, g, setReg, initReg);
            break;
        }
        case STMT_TRY: {
            int varIdx = -1;
            if (stmt->tryStmt.varName.length > 0 && stmt->tryStmt.varName.start) {
                char vname[256];
                snprintf(vname, sizeof(vname), "%.*s", (int)stmt->tryStmt.varName.length, stmt->tryStmt.varName.start);
                varIdx = register_global(comp, vname);
            }
            int tstart = comp->curBC->count;
            emit(comp->curBC, OP_TRY_START, 0, varIdx, 0);
            for (int i = 0; i < stmt->tryStmt.bodyCount; i++)
                compile_stmt(comp, stmt->tryStmt.body[i], break_list, break_count_ptr);
            int tend = comp->curBC->count;
            emit(comp->curBC, OP_TRY_END, 0, 0, 0);
            int tjo = comp->curBC->count;
            emit(comp->curBC, OP_JUMP, 0, 0, 0);
            int tcatch = comp->curBC->count;
            for (int i = 0; i < stmt->tryStmt.handlerCount; i++)
                compile_stmt(comp, stmt->tryStmt.handler[i], break_list, break_count_ptr);
            comp->curBC->code[tjo].r2 = comp->curBC->count;
            for (int i = 0; i < stmt->tryStmt.finallyCount; i++)
                compile_stmt(comp, stmt->tryStmt.finallyBody[i], break_list, break_count_ptr);
            /* bare try (no catch): mark ignore=1 so the VM records swallowed exceptions into the debug slot */
            bytecode_add_try(comp->curBC, tstart, tend, tcatch, varIdx, stmt->tryStmt.handlerCount == 0 ? 1 : 0);
            release_temps(comp);
            break;
        }
        case STMT_THROW: {
            int e = compile_expr(comp, stmt->throwStmt.expr);
            emit(comp->curBC, OP_THROW, e, 0, 0);
            release_temps(comp);
            break;
        }

case STMT_ASSIGN: {

    /* const check: assigning to a const global is a compile error (top-level only) */
    if (!comp->in_function && stmt->assignStmt.target && stmt->assignStmt.target->type == EXPR_IDENT) {
        char nm[256];
        snprintf(nm, sizeof(nm), "%.*s", (int)stmt->assignStmt.target->identName.length, stmt->assignStmt.target->identName.start);
        int g = register_global(comp, nm);
        if (comp_const_is(comp, g)) {
            fprintf(stderr, "Error: cannot assign to const/final '%s'\n", nm);
            exit(1);
        }
    }

            /* record: tagged/with-block global assignment = implicit record declaration */
            if (stmt->assignStmt.target && stmt->assignStmt.target->type == EXPR_IDENT &&
                !comp->in_function && (stmt->assignStmt.tagCount >0 || comp->tag_depth >0)) {
                char nm[256];
                snprintf(nm, sizeof(nm), "%.*s", (int)stmt->assignStmt.target->identName.length, stmt->assignStmt.target->identName.start);
                int g = register_global(comp, nm);
                if (!comp_record_is_reg(comp, g)) {
                    RecordTag *tg = stmt->assignStmt.tags;
                    int tc = stmt->assignStmt.tagCount;
                    if (tc ==0 && comp->tag_depth >0) { tg = comp->tag_stack[comp->tag_depth -1]; tc = comp->tag_stack_count[comp->tag_depth -1]; }
                    int meta = comp_record_meta(comp, tg, tc);
                    int sidx = bytecode_add_string(comp->curBC, nm);
                    int rv;
                    if (stmt->assignStmt.value) rv = compile_expr(comp, stmt->assignStmt.value);
                    else { rv = alloc_reg(); emit(comp->curBC, OP_LOADK_INT, rv, 0, 0); }
                    int rs = alloc_reg();
                    emit(comp->curBC, OP_MOV, rs, rv, 0);
                    emit(comp->curBC, OP_RECORD, g, (meta <<16) | sidx, rs);
                    emit(comp->curBC, OP_STORE_GLOBAL, g, rs, 0);
                    release_temps(comp);
                }
                    break;
                }
            if (stmt->assignStmt.target && stmt->assignStmt.target->type == EXPR_INDEX) {
                if (stmt->assignStmt.target->index.object->type == EXPR_INDEX) {
                    /* 閾惧紡绱㈠紩 a[i][j]... = value锛堣嚜鍔ㄥ垱寤轰腑闂存暟缁勶級 */
                    compile_index_set_chain(comp, stmt->assignStmt.target, stmt->assignStmt.value);
                } else {
                    /* a[i] = value */
                    int arr = compile_expr(comp, stmt->assignStmt.target->index.object);
                    int idx = compile_expr(comp, stmt->assignStmt.target->index.index);
                    int val = compile_expr(comp, stmt->assignStmt.value);
                    emit(comp->curBC, OP_INDEX_SET, arr, idx, val);
                }
            } else if (stmt->assignStmt.target && stmt->assignStmt.target->type == EXPR_IDENT) {
                char name[256];
                snprintf(name, sizeof(name), "%.*s", (int)stmt->assignStmt.target->identName.length,
                         stmt->assignStmt.target->identName.start);
                int local = lookup_local(comp, name);
                if (local >= 0) {
                    /* 灞€閮ㄥ彉閲忥紙鍑芥暟鍐咃級锛氱洿鎺ュ啓瀵勫瓨锟?*/
                    if (stmt->assignStmt.value) {
                        int r = compile_expr(comp, stmt->assignStmt.value);
                        emit(comp->curBC, OP_MOV, local, r, 0);
                    } else {
                        emit(comp->curBC, OP_LOADK_INT, local, 0, 0);  /* int x 澹版槑 -> 0 */
                    }
                } else if (comp->in_function) {
                    /* 鍑芥暟鍐呭彉閲忥細鏄惧紡 global 澹版槑鍒欏啓鍏ㄥ眬锛屽惁鍒欎竴寰嬪眬閮紙Python 寮忥級 */
                    if (lookup_global_decl(comp, name) >= 0) {
                        int g = register_global(comp, name);
                        int r;
                        if (stmt->assignStmt.value) {
                            r = compile_expr(comp, stmt->assignStmt.value);
                        } else {
                            r = alloc_reg();
                            emit(comp->curBC, OP_LOADK_INT, r, 0, 0);
                        }
                        emit(comp->curBC, OP_STORE_GLOBAL, g, r, 0);
                    } else {
                        local = alloc_local(comp, name);
                        if (stmt->assignStmt.value) {
                            int r = compile_expr(comp, stmt->assignStmt.value);
                            emit(comp->curBC, OP_MOV, local, r, 0);
                        } else {
                            emit(comp->curBC, OP_LOADK_INT, local, 0, 0);
                        }
                    }
                } else {
                    int r;
                    if (stmt->assignStmt.value) {
                        r = compile_expr(comp, stmt->assignStmt.value);
                    } else {
                        r = alloc_reg();
                        emit(comp->curBC, OP_NEW_ARRAY, r, 0, 0);
                    }
                    int g_idx = register_global(comp, name);
                    emit(comp->curBC, OP_STORE_GLOBAL, g_idx, r, 0);
                }
                release_temps(comp);  /* 璇彞缁撴潫锛氬洖鏀朵复鏃跺瘎瀛樺櫒 */
            }
                else if (stmt->assignStmt.target && stmt->assignStmt.target->type == EXPR_MEMBER) {
                    /* 鍛藉悕绌洪棿浼樺厛锛歛.b.c = v锛堥摼寮忥紝缂栬瘧鏈熻В鏋愪负甯﹀墠缂€鍏ㄥ眬锛?*/
                    char nsfull_a[512];
                    if (ns_flatten(comp, stmt->assignStmt.target, nsfull_a, sizeof(nsfull_a))) {
                        int g = register_global(comp, nsfull_a);
                        int r;
                        if (stmt->assignStmt.value) r = compile_expr(comp, stmt->assignStmt.value);
                        else { r = alloc_reg(); emit(comp->curBC, OP_NEW_ARRAY, r, 0, 0); }
                        emit(comp->curBC, OP_STORE_GLOBAL, g, r, 0);
                        release_temps(comp);
                    }
                    else if (stmt->assignStmt.target->member.object->type == EXPR_IDENT) {
                    /* namespace global: obj.member = value锛堥潪鍛藉悕绌洪棿鍏滃簳锛?*/
                    char onm[256], mnm[128], full[384];
                    snprintf(onm, sizeof onm, "%.*s", (int)stmt->assignStmt.target->member.object->identName.length, stmt->assignStmt.target->member.object->identName.start);
                    snprintf(mnm, sizeof mnm, "%.*s", (int)stmt->assignStmt.target->member.member.length, stmt->assignStmt.target->member.member.start);
                    if (strcmp(mnm, "type") == 0 || strcmp(mnm, "range") == 0 || strcmp(mnm, "int") == 0 ||
                        strcmp(mnm, "float") == 0 || strcmp(mnm, "str") == 0 || strcmp(mnm, "bool") == 0) {
                        fprintf(stderr, "Error: cannot assign to read-only property '%s.%s'\n", onm, mnm);
                        exit(1);
                    }
                    snprintf(full, sizeof full, "%s.%s", onm, mnm);
                    int g = register_global(comp, full);
                    int r;
                    if (stmt->assignStmt.value) r = compile_expr(comp, stmt->assignStmt.value);
                    else { r = alloc_reg(); emit(comp->curBC, OP_NEW_ARRAY, r, 0, 0); }
                    emit(comp->curBC, OP_STORE_GLOBAL, g, r, 0);
                    release_temps(comp);
                    }
                }
            break;
        }

        case STMT_IF: {
            int cond = compile_expr(comp, stmt->ifStmt.condition);
            int jf = comp->curBC->count;
            emit(comp->curBC, OP_JUMP_IF_FALSE, cond, 0, 0);
            for (int i = 0; i < stmt->ifStmt.thenCount; i++)
                compile_stmt(comp, stmt->ifStmt.thenBody[i], break_list, break_count_ptr);
            int jo = comp->curBC->count;
            emit(comp->curBC, OP_JUMP, 0, 0, 0);
            comp->curBC->code[jf].r2 = comp->curBC->count;
            if (stmt->ifStmt.elseBody)
                for (int i = 0; i < stmt->ifStmt.elseCount; i++)
                    compile_stmt(comp, stmt->ifStmt.elseBody[i], break_list, break_count_ptr);
            comp->curBC->code[jo].r2 = comp->curBC->count;
            release_temps(comp);
            break;
        }

        case STMT_WHILE: {
            int *my_breaks = NULL; int my_bc = 0;
            int *my_cont = NULL; int my_cc = 0;
            int **saved_cl = comp->cont_list, *saved_cc = comp->cont_count;
            comp->cont_list = &my_cont; comp->cont_count = &my_cc;
            int loopStart = comp->curBC->count;
            int cond = compile_expr(comp, stmt->whileStmt.condition);
            int jout = comp->curBC->count;
            emit(comp->curBC, OP_JUMP_IF_FALSE, cond, 0, 0);
            for (int i = 0; i < stmt->whileStmt.bodyCount; i++)
                compile_stmt(comp, stmt->whileStmt.body[i], &my_breaks, &my_bc);
            emit(comp->curBC, OP_JUMP, 0, loopStart, 0);
            int exit = comp->curBC->count;
            comp->curBC->code[jout].r2 = exit;
            for (int i = 0; i < my_bc; i++)
                comp->curBC->code[my_breaks[i]].r2 = exit;
            for (int i = 0; i < my_cc; i++)
                comp->curBC->code[my_cont[i]].r2 = loopStart;
            free(my_breaks);
            free(my_cont);
            comp->cont_list = saved_cl; comp->cont_count = saved_cc;
            release_temps(comp);
            break;
        }

        case STMT_FOR: {
            if (stmt->forStmt.iterExpr) {
                /* for x in <鏁扮粍>锛歩=0; while i<len(arr): x=arr[i]; body; i++ */
                int *my_breaks = NULL; int my_bc = 0;
                int *my_cont = NULL; int my_cc = 0;
                int **saved_cl = comp->cont_list, *saved_cc = comp->cont_count;
                comp->cont_list = &my_cont; comp->cont_count = &my_cc;
                int arr_reg = compile_expr(comp, stmt->forStmt.iterExpr);
                protect_reg(comp, arr_reg);  /* 鏁扮粍寮曠敤璺ㄥ惊鐜綋瀛樻椿 */
                int i_reg, idx_global = -1;
                if (comp->in_function) {
                    i_reg = fresh_local(comp, "__for_i");
                    emit(comp->curBC, OP_LOADK_INT, i_reg, 0, 0);
                } else {
                    idx_global = register_global(comp, "__for_i");
                    i_reg = alloc_reg();
                    emit(comp->curBC, OP_LOADK_INT, i_reg, 0, 0);
                    emit(comp->curBC, OP_STORE_GLOBAL, idx_global, i_reg, 0);
                }
                int loopStart = comp->curBC->count;
                int r_i;
                if (comp->in_function) r_i = i_reg;
                else { r_i = alloc_reg(); emit(comp->curBC, OP_LOAD_GLOBAL, r_i, idx_global, 0); }
                protect_reg(comp, r_i);  /* 寰幆绱㈠紩璺ㄥ惊鐜綋瀛樻椿锛堣鍙ョ骇閲婃斁浼氬洖鏀朵复鏃跺瘎瀛樺櫒锟?*/
                /* 闀垮害锛歭en(arr) */
                emit(comp->curBC, OP_PUSH_REG, arr_reg, 0, 0);
                int r_len = alloc_reg();
                emit(comp->curBC, OP_CALL_BUILTIN, r_len, bytecode_add_string(comp->curBC, "len"), 1);
                int cmp = alloc_reg();
                emit(comp->curBC, OP_LT, cmp, r_i, r_len);
                int jout = comp->curBC->count;
                emit(comp->curBC, OP_JUMP_IF_FALSE, cmp, 0, 0);
                /* x = arr[i] */
                int r_x = alloc_reg();
                emit(comp->curBC, OP_INDEX_GET, r_x, arr_reg, r_i);
                {
                    char var[256];
                    snprintf(var, sizeof(var), "%.*s", (int)stmt->forStmt.var.length, stmt->forStmt.var.start);
                    int local = lookup_local(comp, var);
                    if (local >= 0) emit(comp->curBC, OP_MOV, local, r_x, 0);
                    else if (comp->in_function) {
                        if (lookup_global_decl(comp, var) >= 0) { int g = register_global(comp, var); emit(comp->curBC, OP_STORE_GLOBAL, g, r_x, 0); }
                        else { local = alloc_local(comp, var); emit(comp->curBC, OP_MOV, local, r_x, 0); }
                    }
                    else { int g = register_global(comp, var); emit(comp->curBC, OP_STORE_GLOBAL, g, r_x, 0); }
                }
                for (int i = 0; i < stmt->forStmt.bodyCount; i++)
                    compile_stmt(comp, stmt->forStmt.body[i], &my_breaks, &my_bc);
                release_temps(comp);  /* 寰幆浣撹鍙ラ棿鐨勪复鏃跺洖锟?*/
                int contTarget = comp->curBC->count; /* continue: skip to increment */
                int one = alloc_reg();
                emit(comp->curBC, OP_LOADK_INT, one, 1, 0);
                if (comp->in_function) {
                    emit(comp->curBC, OP_ADD, i_reg, r_i, one);
                } else {
                    int new_i = alloc_reg();
                    emit(comp->curBC, OP_ADD, new_i, r_i, one);
                    emit(comp->curBC, OP_STORE_GLOBAL, idx_global, new_i, 0);
                }
                emit(comp->curBC, OP_JUMP, 0, loopStart, 0);
                int exit = comp->curBC->count;
                comp->curBC->code[jout].r2 = exit;
                for (int i = 0; i < my_bc; i++)
                    comp->curBC->code[my_breaks[i]].r2 = exit;
                for (int i = 0; i < my_cc; i++)
                    comp->curBC->code[my_cont[i]].r2 = contTarget;
                free(my_cont);
                comp->cont_list = saved_cl; comp->cont_count = saved_cc;
                free(my_breaks);
                break;
            }
            char var[256];
            snprintf(var, sizeof(var), "%.*s", (int)stmt->forStmt.var.length, stmt->forStmt.var.start);
            int local = lookup_local(comp, var);
            int idx = -1;   /* 浠呬富绋嬪簭浣跨敤鍏ㄥ眬锟?*/
            if (local >= 0) {
                if (stmt->forStmt.rangeStart) {
                    int r = compile_expr(comp, stmt->forStmt.rangeStart);
                    emit(comp->curBC, OP_MOV, local, r, 0);
                } else {
                    emit(comp->curBC, OP_LOADK_INT, local, 0, 0);
                }
            } else if (comp->in_function) {
                /* 鍑芥暟鍐呭惊鐜彉閲忥細global 澹版槑鍒欏啓鍏ㄥ眬锛屽惁鍒欏眬锟?*/
                if (lookup_global_decl(comp, var) >= 0) {
                    int g = register_global(comp, var);
                    if (stmt->forStmt.rangeStart) {
                        int r = compile_expr(comp, stmt->forStmt.rangeStart);
                        emit(comp->curBC, OP_STORE_GLOBAL, g, r, 0);
                    } else {
                        int r = alloc_reg();
                        emit(comp->curBC, OP_LOADK_INT, r, 0, 0);
                        emit(comp->curBC, OP_STORE_GLOBAL, g, r, 0);
                    }
                } else {
                    local = alloc_local(comp, var);
                    if (stmt->forStmt.rangeStart) {
                        int r = compile_expr(comp, stmt->forStmt.rangeStart);
                        emit(comp->curBC, OP_MOV, local, r, 0);
                    } else {
                        emit(comp->curBC, OP_LOADK_INT, local, 0, 0);
                    }
                }
            } else {
                idx = register_global(comp, var);
                if (stmt->forStmt.rangeStart) {
                    int r = compile_expr(comp, stmt->forStmt.rangeStart);
                    emit(comp->curBC, OP_STORE_GLOBAL, idx, r, 0);
                } else {
                    int r = alloc_reg();
                    emit(comp->curBC, OP_LOADK_INT, r, 0, 0);
                    emit(comp->curBC, OP_STORE_GLOBAL, idx, r, 0);
                }
            }
            int *my_cont = NULL; int my_cc = 0;
            int **saved_cl = comp->cont_list, *saved_cc = comp->cont_count;
            comp->cont_list = &my_cont; comp->cont_count = &my_cc;
            int *my_breaks = NULL; int my_bc = 0;
            int loopStart = comp->curBC->count;
            int reg_idx;
            if (local >= 0) reg_idx = local;
            else { reg_idx = alloc_reg(); emit(comp->curBC, OP_LOAD_GLOBAL, reg_idx, idx, 0); }
            int end_reg = compile_expr(comp, stmt->forStmt.rangeEnd);
            protect_reg(comp, end_reg);  /* 缁撴潫鍊艰法寰幆浣撳瓨锟?*/
            int step_reg = -1, step_neg = 0;
            if (stmt->forStmt.rangeStep) {
                step_reg = compile_expr(comp, stmt->forStmt.rangeStep);
                protect_reg(comp, step_reg);
                if (stmt->forStmt.rangeStep->type == EXPR_NUMBER && stmt->forStmt.rangeStep->intVal < 0) step_neg = 1;
                else if (stmt->forStmt.rangeStep->type == EXPR_UNARY && stmt->forStmt.rangeStep->unary.op == TOK_MINUS
                         && stmt->forStmt.rangeStep->unary.operand && stmt->forStmt.rangeStep->unary.operand->type == EXPR_NUMBER) step_neg = 1;
            }
            int cmp_reg = alloc_reg();
            emit(comp->curBC, step_neg ? OP_GT : OP_LT, cmp_reg, reg_idx, end_reg);
            int jout = comp->curBC->count;
            emit(comp->curBC, OP_JUMP_IF_FALSE, cmp_reg, 0, 0);
            for (int i = 0; i < stmt->forStmt.bodyCount; i++)
                compile_stmt(comp, stmt->forStmt.body[i], &my_breaks, &my_bc);
            int contTarget = comp->curBC->count; /* continue: skip to increment */
            int one_reg = alloc_reg();
            if (step_reg >= 0) emit(comp->curBC, OP_MOV, one_reg, step_reg, 0);
            else emit(comp->curBC, OP_LOADK_INT, one_reg, 1, 0);
            if (local >= 0) {
                emit(comp->curBC, OP_ADD, local, reg_idx, one_reg);
            } else {
                int new_idx = alloc_reg();
                emit(comp->curBC, OP_ADD, new_idx, reg_idx, one_reg);
                emit(comp->curBC, OP_STORE_GLOBAL, idx, new_idx, 0);
            }
            emit(comp->curBC, OP_JUMP, 0, loopStart, 0);
            int exit = comp->curBC->count;
            comp->curBC->code[jout].r2 = exit;
            for (int i = 0; i < my_bc; i++)
                comp->curBC->code[my_breaks[i]].r2 = exit;
            for (int i = 0; i < my_cc; i++)
                comp->curBC->code[my_cont[i]].r2 = contTarget;
            free(my_cont);
            comp->cont_list = saved_cl; comp->cont_count = saved_cc;
            free(my_breaks);
            release_temps(comp);
            break;
        }

        case STMT_REPEAT: {
            int *my_breaks = NULL; int my_bc = 0;
            int *my_cont = NULL; int my_cc = 0;
            int **saved_cl = comp->cont_list, *saved_cc = comp->cont_count;
            comp->cont_list = &my_cont; comp->cont_count = &my_cc;
            int reg_counter, counter_idx = -1;
            if (comp->in_function) {
                reg_counter = fresh_local(comp, "__repeat_cnt");
                emit(comp->curBC, OP_LOADK_INT, reg_counter, 0, 0);
            } else {
                counter_idx = register_global(comp, "__repeat_cnt");
                reg_counter = alloc_reg();
                emit(comp->curBC, OP_LOADK_INT, reg_counter, 0, 0);
                emit(comp->curBC, OP_STORE_GLOBAL, counter_idx, reg_counter, 0);
            }
            int loopStart = comp->curBC->count;
            int r_cnt;
            if (comp->in_function) r_cnt = reg_counter;
            else { r_cnt = alloc_reg(); emit(comp->curBC, OP_LOAD_GLOBAL, r_cnt, counter_idx, 0); }
            protect_reg(comp, r_cnt);
            int r_max = compile_expr(comp, stmt->repeatStmt.count);
            int cmp = alloc_reg();
            emit(comp->curBC, OP_LT, cmp, r_cnt, r_max);
            int jout = comp->curBC->count;
            emit(comp->curBC, OP_JUMP_IF_FALSE, cmp, 0, 0);
            for (int i = 0; i < stmt->repeatStmt.bodyCount; i++)
                compile_stmt(comp, stmt->repeatStmt.body[i], &my_breaks, &my_bc);
            int contTarget = comp->curBC->count; /* continue: skip to increment */
            int one = alloc_reg();
            emit(comp->curBC, OP_LOADK_INT, one, 1, 0);
            if (comp->in_function) {
                emit(comp->curBC, OP_ADD, reg_counter, r_cnt, one);
            } else {
                int new_cnt = alloc_reg();
                emit(comp->curBC, OP_ADD, new_cnt, r_cnt, one);
                emit(comp->curBC, OP_STORE_GLOBAL, counter_idx, new_cnt, 0);
            }
            emit(comp->curBC, OP_JUMP, 0, loopStart, 0);
            int exit = comp->curBC->count;
            comp->curBC->code[jout].r2 = exit;
            for (int i = 0; i < my_bc; i++)
                comp->curBC->code[my_breaks[i]].r2 = exit;
            for (int i = 0; i < my_cc; i++)
                comp->curBC->code[my_cont[i]].r2 = contTarget;


            free(my_breaks);
            free(my_cont);


            comp->cont_list = saved_cl; comp->cont_count = saved_cc;
            break;
        }

        case STMT_DO_UNTIL: {
            int *my_breaks = NULL; int my_bc = 0;
            int *my_cont = NULL; int my_cc = 0;
            int **saved_cl = comp->cont_list, *saved_cc = comp->cont_count;
            comp->cont_list = &my_cont; comp->cont_count = &my_cc;
            int loopStart = comp->curBC->count;
            for (int i = 0; i < stmt->doUntilStmt.bodyCount; i++)
                compile_stmt(comp, stmt->doUntilStmt.body[i], &my_breaks, &my_bc);
            int contTarget = comp->curBC->count; /* continue: re-check condition */
            int cond = compile_expr(comp, stmt->doUntilStmt.condition);
            emit(comp->curBC, OP_JUMP_IF_FALSE, cond, loopStart, 0);
            int exit = comp->curBC->count;
            for (int i = 0; i < my_bc; i++)
                comp->curBC->code[my_breaks[i]].r2 = exit;
            for (int i = 0; i < my_cc; i++)
                comp->curBC->code[my_cont[i]].r2 = contTarget;
            free(my_breaks);
            free(my_cont);
            comp->cont_list = saved_cl; comp->cont_count = saved_cc;
            break;
        }


        case STMT_WAIT_UNTIL: {
            int *my_cont = NULL; int my_cc = 0;
            int **saved_cl = comp->cont_list, *saved_cc = comp->cont_count;
            comp->cont_list = &my_cont; comp->cont_count = &my_cc;
            int *my_breaks = NULL; int my_bc = 0;
            int loopStart = comp->curBC->count;
            int cond = compile_expr(comp, stmt->waitUntilStmt.condition);
            int jout = comp->curBC->count;
            emit(comp->curBC, OP_JUMP_IF_TRUE, cond, 0, 0);
            int wait_reg = alloc_reg();
            emit(comp->curBC, OP_LOADK_FLOAT, wait_reg, bytecode_add_float(comp->curBC, 0.1), 0);
            emit(comp->curBC, OP_WAIT, wait_reg, 0, 0);
            emit(comp->curBC, OP_JUMP, 0, loopStart, 0);
            int exit = comp->curBC->count;
            comp->curBC->code[jout].r2 = exit;
            for (int i = 0; i < my_bc; i++)
                comp->curBC->code[my_breaks[i]].r2 = exit;
            for (int i = 0; i < my_cc; i++)
                comp->curBC->code[my_cont[i]].r2 = loopStart;
            free(my_cont);
            comp->cont_list = saved_cl; comp->cont_count = saved_cc;
            free(my_breaks);
            break;
        }

        case STMT_BREAK: {
            if (stmt->breakStmt.label) {
                /* break A: jump to label end (patched later) */
                int pos = comp->curBC->count;
                emit(comp->curBC, OP_JUMP, 0, 0, 0);
                comp->patches = realloc(comp->patches, (comp->patchCount + 1) * sizeof(LabelPatch));
                comp->patches[comp->patchCount].jump_pos = pos;
                comp->patches[comp->patchCount].label = strdup(stmt->breakStmt.label);
                comp->patches[comp->patchCount].kind = 1;
                comp->patchCount++;
            } else {
                int pos = comp->curBC->count;
                emit(comp->curBC, OP_JUMP, 0, 0, 0);
                add_break(break_list, break_count_ptr, pos);
            }
            break;
        }

        case STMT_CONTINUE: {
            if (comp->cont_list) {
                int pos = comp->curBC->count;
                emit(comp->curBC, OP_JUMP, 0, 0, 0);
                *comp->cont_list = realloc(*comp->cont_list, (*comp->cont_count + 1) * sizeof(int));
                (*comp->cont_list)[(*comp->cont_count)++] = pos;
            }
            break;
        }

        case STMT_LABEL: {
            int start = comp->curBC->count;
            char lfull[512];
            ns_full(comp, stmt->labelStmt.name, lfull, sizeof(lfull));
            comp->labels = realloc(comp->labels, (comp->labelCount + 1) * sizeof(LabelDef));
            comp->labels[comp->labelCount].name = strdup(lfull);
            comp->labels[comp->labelCount].start_off = start;
            comp->labels[comp->labelCount].end_off = -1;
            comp->labelCount++;
            for (int i = 0; i < stmt->labelStmt.bodyCount; i++)
                compile_stmt(comp, stmt->labelStmt.body[i], break_list, break_count_ptr);
            int end = comp->curBC->count;
            for (int i = 0; i < comp->labelCount; i++)
                if (strcmp(comp->labels[i].name, stmt->labelStmt.name) == 0)
                    comp->labels[i].end_off = end;
            break;
        }

        case STMT_GOTO_LABEL: {
            int pos = comp->curBC->count;
            char lfull[512];
            ns_full(comp, stmt->gotoStmt.label, lfull, sizeof(lfull));
            emit(comp->curBC, OP_JUMP, 0, 0, 0);
            comp->patches = realloc(comp->patches, (comp->patchCount + 1) * sizeof(LabelPatch));
            comp->patches[comp->patchCount].jump_pos = pos;
            comp->patches[comp->patchCount].label = strdup(lfull);
            comp->patches[comp->patchCount].kind = 0;
            comp->patchCount++;
            break;
        }

        case STMT_THREAD_GOTO: {
            int tidx = -1;
            char tfull[512], lfull[512];
            ns_full(comp, stmt->threadGotoStmt.thread, tfull, sizeof(tfull));
            ns_full(comp, stmt->threadGotoStmt.label, lfull, sizeof(lfull));
            for (int i = 0; i < comp->mainBC->thread_count; i++)
                if (comp->mainBC->thread_names[i] && strcmp(comp->mainBC->thread_names[i], tfull) == 0) { tidx = i; break; }
            if (tidx < 0) {
                fprintf(stderr, "error: unknown thread '%s' in 'to'\n", tfull);
                exit(1);
            }
            emit(comp->curBC, OP_THREAD_GOTO, tidx, 0, 0);
            comp->patches = realloc(comp->patches, (comp->patchCount + 1) * sizeof(LabelPatch));
            comp->patches[comp->patchCount].jump_pos = comp->curBC->count - 1;
            comp->patches[comp->patchCount].label = strdup(lfull);
            comp->patches[comp->patchCount].kind = 2;
            comp->patchCount++;
            break;
        }

        case STMT_EXPR:
            compile_expr(comp, stmt->exprStmt.expr);
            break;

        case STMT_FUNC:
            /* 鍑芥暟瀹氫箟锛氫富绋嬪簭涓嶅彂灏勬寚浠わ紙鍑芥暟浣撳湪绗笁閬嶅崟鐙紪璇戯級 */
            break;

        case STMT_RETURN: {
            if (stmt->returnStmt.value) {
                int r = compile_expr(comp, stmt->returnStmt.value);
                emit(comp->curBC, OP_RETURN, r, 0, 0);
            } else {
                emit(comp->curBC, OP_RETURN, 0, 0, 0);
            }
            release_temps(comp);
            break;
        }

        default: break;
    }
}

/* ---------- 缂栬瘧鍣ㄦ帴锟?---------- */
Compiler *compiler_new(void) {
    Compiler *comp = malloc(sizeof(Compiler));
    memset(comp, 0, sizeof(Compiler)); /* ensure tag_stack/tag_depth/record_flags/const_flags/gdecl are zeroed */
    comp->mainBC = malloc(sizeof(Bytecode));
    bytecode_init(comp->mainBC);
    comp->curBC = comp->mainBC;
    comp->globals = NULL; comp->globalCount = 0; comp->globalCap = 0;
    /* preset builtin set globals (must match vm.c vm_init order: N Z Z+ Z- Float1..9 float1..9 kong) */
    {
        static const char *pset[23] = {
            "N","Z","Z+","Z-",
            "Float1","float1","Float2","float2","Float3","float3","Float4","float4",
            "Float5","float5","Float6","float6","Float7","float7","Float8","float8","Float9","float9",
            "\xBF\xD5"
        };
        for (int i = 0; i < 23; i++) {
            if (comp->globalCount >= comp->globalCap) {
                comp->globalCap = comp->globalCap == 0 ? 32 : comp->globalCap * 2;
                comp->globals = realloc(comp->globals, comp->globalCap * sizeof(GlobalVar));
            }
            comp->globals[comp->globalCount].name = strdup(pset[i]);
            comp->globals[comp->globalCount].index = comp->globalCount;
            comp->globalCount++;
        }
    }
    comp->localCount = 0;
    comp->in_function = 0;
    comp->builtinCount = 0;
    comp->builtins[comp->builtinCount++].name = strdup("random");
    comp->builtins[comp->builtinCount++].name = strdup("sqrt");
    comp->builtins[comp->builtinCount++].name = strdup("round");
    comp->builtins[comp->builtinCount++].name = strdup("read_file");
    comp->builtins[comp->builtinCount++].name = strdup("write_file");
    comp->builtins[comp->builtinCount++].name = strdup("input");
    comp->builtins[comp->builtinCount++].name = strdup("int");
    comp->builtins[comp->builtinCount++].name = strdup("float");
    comp->builtins[comp->builtinCount++].name = strdup("str");
    comp->builtins[comp->builtinCount++].name = strdup("bool");
    comp->builtins[comp->builtinCount++].name = strdup("len");
    comp->builtins[comp->builtinCount++].name = strdup("size");
    comp->builtins[comp->builtinCount++].name = strdup("list");
    comp->builtins[comp->builtinCount++].name = strdup("sum");
    comp->builtins[comp->builtinCount++].name = strdup("push");
    comp->builtins[comp->builtinCount++].name = strdup("pop");
    comp->builtins[comp->builtinCount++].name = strdup("join");
    comp->builtins[comp->builtinCount++].name = strdup("split");
    comp->builtins[comp->builtinCount++].name = strdup("build");
    comp->builtins[comp->builtinCount++].name = strdup("window");
    comp->builtins[comp->builtinCount++].name = strdup("show_image");
    comp->builtins[comp->builtinCount++].name = strdup("gui_wait");

    comp->usingMods = NULL;
    comp->usingCount = 0;
    comp->threadCount = 0;
    comp->mutexCount = 0;
    comp->cur_thread_argc = 0;
    comp->in_thread = 0;
    return comp;
}

void compiler_free(Compiler *comp) {
    bytecode_free(comp->mainBC);
    free(comp->mainBC);
    for (int i = 0; i < comp->globalCount; i++) free(comp->globals[i].name);
    free(comp->globals);
    for (int i = 0; i < comp->localCount; i++) free(comp->locals[i].name);
    for (int i = 0; i < comp->builtinCount; i++) free(comp->builtins[i].name);
    for (int i = 0; i < comp->usingCount; i++) free(comp->usingMods[i]);
    free(comp->usingMods);
    for (int i = 0; i < comp->ns_visible_cap; i++) free(comp->ns_visible[i]);
    free(comp->ns_visible);
    for (int i = 0; i < comp->import_count; i++) free(comp->imports[i].key);
    free(comp->imports);
    free(comp);
}

/* 閫掑綊瀹氫綅 import 瀛愭ā鍧楃殑 Program锛坘ey = 璺緞 + '\x01' + ns锛?*/
static Program *find_import_prog(Compiler *comp, Stmt *s, char *key_out, size_t key_sz) {
    char rel[512], ns[256], key[1100];
    snprintf(rel, sizeof(rel), "%.*s", (int)s->importStmt.path.length, s->importStmt.path.start);
    ns[0] = 0;
    int has_ns = (s->type == STMT_IMPORT && s->importStmt.ns.length > 0);
    if (has_ns) snprintf(ns, sizeof(ns), "%.*s", (int)s->importStmt.ns.length, s->importStmt.ns.start);
    char *full = resolve_import_path(comp->cur_dir, rel);
    snprintf(key, sizeof(key), "%s\x01%s\x01%s", full, has_ns ? ns : "", comp->cur_ns);
    free(full);
    if (key_out) snprintf(key_out, key_sz, "%s", key);
    for (int k = 0; k < comp->import_count; k++)
        if (strcmp(comp->imports[k].key, key) == 0) return comp->imports[k].prog;
    return NULL;
}

/* 绗笁/鍥涢亶锛氶€掑綊缂栬瘧鍑芥暟浣?绾跨▼浣擄紙import 瀛愭ā鍧楅€掑綊锛屽懡鍚嶇┖闂村墠缂€/鐩綍鍒囨崲锛夈€?
   which: 0=鍑芥暟浣?1=绾跨▼浣?*/
static void compile_bodies(Compiler *comp, Program *prog, int which) {
    for (int i = 0; i < prog->count; i++) {
        Stmt *s = prog->stmts[i];
        if (which == 0 && s->type == STMT_FUNC)
            compile_func_body(comp, s);
        else if (which == 1 && s->type == STMT_THREAD_DEF)
            compile_thread_body(comp, s);
        else if (s->type == STMT_IMPORT || s->type == STMT_INCLUDE) {
            char key[1100];
            Program *sub = find_import_prog(comp, s, key, sizeof(key));
            if (!sub) continue;
            char rel[512], ns[256];
            snprintf(rel, sizeof(rel), "%.*s", (int)s->importStmt.path.length, s->importStmt.path.start);
            ns[0] = 0;
            int has_ns = (s->type == STMT_IMPORT && s->importStmt.ns.length > 0);
            if (has_ns) snprintf(ns, sizeof(ns), "%.*s", (int)s->importStmt.ns.length, s->importStmt.ns.start);
            char saved_ns[512], saved_dir[1024];
            strcpy(saved_ns, comp->cur_ns);
            strcpy(saved_dir, comp->cur_dir);
            if (has_ns) {
                strncat(comp->cur_ns, ns, sizeof(comp->cur_ns) - strlen(comp->cur_ns) - 1);
                strncat(comp->cur_ns, ".", sizeof(comp->cur_ns) - strlen(comp->cur_ns) - 1);
            }
            char *full = resolve_import_path(saved_dir, rel);
            dir_of(full, comp->cur_dir, sizeof(comp->cur_dir));
            free(full);
            compile_bodies(comp, sub, which);
            strcpy(comp->cur_ns, saved_ns);
            strcpy(comp->cur_dir, saved_dir);
        }
    }
}

/* 绗竴閬嶆敹闆嗭細閫掑綊鏀堕泦鍑芥暟/绾跨▼鍚嶏紙import/include 閫掑綊锛屽懡鍚嶇┖闂村墠缂€鍒囨崲锛夈€?
   imports 鏉＄洰 state: 0=杩涜涓?1=宸叉敹闆?2=宸茬紪璇?*/
static void collect_decls(Compiler *comp, Program *prog) {
    for (int i = 0; i < prog->count; i++) {
        Stmt *s = prog->stmts[i];
        if (s->type == STMT_FUNC) {
            char name[256];
            snprintf(name, sizeof(name), "%.*s", (int)s->funcDef.name.length, s->funcDef.name.start);
            register_func(comp, name);
        } else if (s->type == STMT_THREAD_DEF) {
            char name[256];
            snprintf(name, sizeof(name), "%.*s", (int)s->threadDef.name.length, s->threadDef.name.start);
            register_thread(comp, name);
        } else if (s->type == STMT_IMPORT || s->type == STMT_INCLUDE) {
            char rel[512], ns[256], key[1100];
            snprintf(rel, sizeof(rel), "%.*s", (int)s->importStmt.path.length, s->importStmt.path.start);
            ns[0] = 0;
            int has_ns = (s->type == STMT_IMPORT && s->importStmt.ns.length > 0);
            if (has_ns) snprintf(ns, sizeof(ns), "%.*s", (int)s->importStmt.ns.length, s->importStmt.ns.start);
            char *full = resolve_import_path(comp->cur_dir, rel);
            snprintf(key, sizeof(key), "%s\x01%s\x01%s", full, has_ns ? ns : "", comp->cur_ns);
            int found = -1;
            for (int k = 0; k < comp->import_count; k++)
                if (strcmp(comp->imports[k].key, key) == 0) { found = k; break; }
            if (found >= 0) {
                if (comp->imports[found].state == 0) {
                    if (has_ns) {
                        fprintf(stderr, "[error] circular import: '%s' as '%s'\n", rel, ns);
                        exit(1);
                    }
                    /* include 鐜細闈欓粯璺宠繃锛堜繚鎸佹棫璇箟锛?*/
                }
                free(full);
                continue;
            }
            int entry_idx = comp->import_count;
            comp->imports = realloc(comp->imports, (comp->import_count + 1) * sizeof(*comp->imports));
            comp->imports[entry_idx].key = strdup(key);
            comp->imports[entry_idx].state = 0;
            comp->imports[entry_idx].prog = NULL;
            comp->import_count++;
            char *src = inim_load_text(full);
            if (!src) {
                fprintf(stderr, "閿欒: 鏃犳硶璇诲彇鏂囦欢 '%s'\n", full);
                exit(1);
            }
            Program *sub = parse_program(src);
            if (!sub) {
                fprintf(stderr, "閿欒: 鏃犳硶瑙ｆ瀽鏂囦欢 '%s'\n", full);
                exit(1);
            }
            /* 涓?free(src)锛欰ST StringView 鎸囧悜婧愮爜缂撳啿鍖猴紝闇€瀛樻椿鍒扮紪璇戝畬鎴?*/
            comp->imports[entry_idx].prog = sub;
            char saved_ns[512], saved_dir[1024];
            strcpy(saved_ns, comp->cur_ns);
            strcpy(saved_dir, comp->cur_dir);
            if (has_ns) {
                strncat(comp->cur_ns, ns, sizeof(comp->cur_ns) - strlen(comp->cur_ns) - 1);
                strncat(comp->cur_ns, ".", sizeof(comp->cur_ns) - strlen(comp->cur_ns) - 1);
            }
            dir_of(full, comp->cur_dir, sizeof(comp->cur_dir));
            collect_decls(comp, sub);
            strcpy(comp->cur_ns, saved_ns);
            strcpy(comp->cur_dir, saved_dir);
            comp->imports[entry_idx].state = 1;
            free(full);
        }
    }
}

void compiler_compile(Compiler *comp, Program *prog) {
    /* 绗竴閬嶏細閫掑綊鏀堕泦鎵€鏈夊嚱鏁板悕涓庣嚎绋嬪悕锛坕mport/include 閫掑綊锛屾敮鎸佸墠鍚戝紩鐢?閫掑綊锛?*/
    collect_decls(comp, prog);
    /* 绗簩閬嶏細缂栬瘧涓荤▼搴忥紙璺宠繃鍑芥暟/绾跨▼瀹氫箟锛沵ain{} 锟?body 锟?compile_stmt 涓睍寮€锟?*/
    reset_regs();
    comp->curBC = comp->mainBC;
    comp->in_function = 0;
    comp->in_thread = 0;
    comp->localCount = 0;
    comp->local_peak = 0;
    int *top_breaks = NULL; int top_bc = 0;
    for (int i = 0; i < prog->count; i++) {
        Stmt *s = prog->stmts[i];
        if (s->type == STMT_FUNC || s->type == STMT_THREAD_DEF) continue;
        compile_stmt(comp, s, &top_breaks, &top_bc);
    }
    /* 娓告垙妯″紡(锟?on 绮剧伒绾跨▼): 涓荤嚎绋嬫敞鍏ョ瓑寰呭惊锟?淇濇寔绮剧伒绾跨▼杩愯 */
    {
        int has_sprite = 0;
        for (int i = 0; i < prog->count; i++)
            if (prog->stmts[i]->type == STMT_GUI &&
                prog->stmts[i]->guiStmt.verb.length == 2 &&
                strncmp(prog->stmts[i]->guiStmt.verb.start, "on", 2) == 0) { has_sprite = 1; break; }
        if (has_sprite) {
            int start = comp->curBC->count;
            int cond = alloc_reg();
            emit(comp->curBC, OP_LOADK_BOOL, cond, 1, 0);
            int jf = comp->curBC->count;
            emit(comp->curBC, OP_JUMP_IF_FALSE, cond, 0, 0);
            int wr = alloc_reg();
            emit(comp->curBC, OP_LOADK_FLOAT, wr, bytecode_add_float(comp->curBC, 0.1), 0);
            emit(comp->curBC, OP_WAIT, wr, 0, 0);
            emit(comp->curBC, OP_JUMP, 0, start, 0);
            comp->curBC->code[jf].r2 = comp->curBC->count;
        } else {
            emit(comp->curBC, OP_HALT, 0, 0, 0);
        }
    }
    free(top_breaks);
    resolve_labels(comp); /* main program labels */
    /* 绗笁閬嶏細缂栬瘧鍚勫嚱鏁颁綋锛堥€掑綊鍚?import 瀛愭ā鍧楋級 */
    compile_bodies(comp, prog, 0);
    /* 绗洓閬嶏細缂栬瘧鍚勭嚎绋嬩綋锛堥€掑綊鍚?import 瀛愭ā鍧楋級 */
    compile_bodies(comp, prog, 1);
    resolve_thread_gotos(comp); /* thread1 to A */
    /* store global names into main bytecode (for debug var / runtime) */
    if (comp->globalCount > 0) {
        comp->mainBC->global_names = malloc(comp->globalCount * sizeof(char*));
        comp->mainBC->global_name_count = comp->globalCount;
        for (int i = 0; i < comp->globalCount; i++)
            comp->mainBC->global_names[i] = comp->globals[i].name ? strdup(comp->globals[i].name) : strdup("?");
    for (int _si = 0; _si < comp->mainBC->string_count && _si < 16; _si++) fprintf(stderr, "[%d]=\"%s\" ", _si, comp->mainBC->string_pool[_si] ? comp->mainBC->string_pool[_si] : "?");
    fprintf(stderr, "\n");
    for (int _di = 0; _di < comp->mainBC->count && _di < 40; _di++) {
        RegInstruction *_in = &comp->mainBC->code[_di];
    }
    for (int _f = 0; _f < comp->mainBC->func_count; _f++) {
        Bytecode *_fb = comp->mainBC->funcs[_f];
    }
    }
}


Bytecode *compiler_get_main_bytecode(Compiler *comp) { return comp->curBC; }

char *compiler_get_using_mods(Compiler *comp) {
    if (comp->usingCount == 0) return NULL;
    size_t total = 1;
    for (int i = 0; i < comp->usingCount; i++)
        total += strlen(comp->usingMods[i]) + 1;
    char *buf = malloc(total);
    buf[0] = '\0';
    for (int i = 0; i < comp->usingCount; i++) {
        if (i > 0) strcat(buf, ",");
        strcat(buf, comp->usingMods[i]);
    }
    return buf;
                    release_temps(comp);
                }
