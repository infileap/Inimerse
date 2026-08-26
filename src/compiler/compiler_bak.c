#include "compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int next_register = 1;
static int reg_peak = 0;
static int alloc_reg(void) { if (next_register >= 1024) { fprintf(stderr, "\n[error] registers overflow (>=1024), abort compile.\n"); exit(1); } if (next_register > reg_peak) reg_peak = next_register; return next_register++; }
static void reset_regs(void) { next_register = 1; reg_peak = 0; }

/* 涓存椂鍙橀噺浼樺寲锛?
   - protect_reg(r)锛氭妸 r 鎻愬崌涓烘寔涔呭瘎瀛樺櫒锛堣法璇彞瀛樻椿锛屽寰幆鏁扮粍/缁撴潫鍊硷級
   - release_temps()锛氬洖鏀舵墍鏈夊尶鍚嶄复鏃跺瘎瀛樺櫒锛堝洖閫€姘翠綅鍒版寔涔呮按浣嶏級锛?
     璇彞缂栬瘧缁撴潫/琛ㄨ揪寮忓瓙鑺傜偣娑堣垂鍚庤皟鐢?*/
static void protect_reg(Compiler *comp, int r) {
    if (r + 1 > comp->local_peak) comp->local_peak = r + 1;
}

static void release_temps(Compiler *comp) {
    if (next_register > comp->local_peak)
        next_register = comp->local_peak;
    if (next_register < 1) next_register = 1;  /* 瀵勫瓨鍣?淇濈暀锛岄伩鍏嶉《灞傝剼鏈?release 鍚?alloc_reg 杩斿洖0 */
}

/* 鍥為€€姘翠綅鍒?level锛堥噴鏀捐姘翠綅涔嬪悗鐨勫尶鍚嶄复鏃跺瘎瀛樺櫒锛夛紝浣嗕笉浣庝簬鎸佷箙姘翠綅 */
static void release_to(Compiler *comp, int level) {
    if (next_register > level) next_register = level;
    if (next_register < comp->local_peak) next_register = comp->local_peak;
    if (next_register < 1) next_register = 1;  /* 瀵勫瓨鍣?淇濈暀 */
}

/* 鎶婂眬閮ㄥ彉閲忔敞鍐岃繘 locals 琛紙鍒嗛厤瀵勫瓨鍣ㄥ苟鎻愬崌鎸佷箙姘翠綅锛?*/
static int alloc_local(Compiler *comp, const char *name) {
    int r = alloc_reg();
    if (comp->localCount < 256) {
        comp->locals[comp->localCount].name = strdup(name);
        comp->locals[comp->localCount].reg = r;
        comp->localCount++;
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

static int register_global(Compiler *comp, const char *name) {
    for (int i = 0; i < comp->globalCount; i++)
        if (strcmp(comp->globals[i].name, name) == 0) return comp->globals[i].index;
    if (comp->globalCount >= comp->globalCap) {
        comp->globalCap = comp->globalCap == 0 ? 16 : comp->globalCap * 2;
        comp->globals = realloc(comp->globals, comp->globalCap * sizeof(GlobalVar));
    }
    comp->globals[comp->globalCount].name = strdup(name);
    comp->globals[comp->globalCount].index = comp->globalCount;
    return comp->globalCount++;
}

/* 鍙煡鍏ㄥ眬锛堜笉鍒涘缓锛夛細杩斿洖绱㈠紩锛屼笉瀛樺湪杩斿洖 -1 */
static int lookup_global_idx(Compiler *comp, const char *name) {
    for (int i = 0; i < comp->globalCount; i++)
        if (strcmp(comp->globals[i].name, name) == 0) return comp->globals[i].index;
    return -1;
}

/* 鍑芥暟鍐?global 澹版槑妫€鏌ワ細鏄惧紡澹版槑鍚庡啓鍏ㄥ眬锛屾湭澹版槑璧嬪€间竴寰嬪眬閮紙Python 寮忥級 */
static int lookup_global_decl(Compiler *comp, const char *name) {
    for (int i = 0; i < comp->gdeclCount; i++)
        if (strcmp(comp->gdecl[i], name) == 0) return i;
    return -1;
}

/* ---------- 鑷畾涔夊嚱鏁?---------- */
static int register_func(Compiler *comp, const char *name) {
    for (int i = 0; i < comp->mainBC->func_count; i++)
        if (strcmp(comp->mainBC->func_names[i], name) == 0) return i;
    if (comp->mainBC->func_count >= 64) return -1;
    int idx = comp->mainBC->func_count++;
    comp->mainBC->func_names[idx] = strdup(name);
    comp->mainBC->funcs[idx] = NULL;
    comp->mainBC->func_argc[idx] = 0;
    return idx;
}

static int lookup_func(Compiler *comp, const char *name) {
    for (int i = 0; i < comp->mainBC->func_count; i++)
        if (strcmp(comp->mainBC->func_names[i], name) == 0) return i;
    return -1;
}

/* ---------- 绾跨▼涓庝簰鏂ラ攣 ---------- */
static int register_thread(Compiler *comp, const char *name) {
    for (int i = 0; i < comp->mainBC->thread_count; i++)
        if (strcmp(comp->mainBC->thread_names[i], name) == 0) return i;
    if (comp->mainBC->thread_count >= 32) return -1;
    int idx = comp->mainBC->thread_count++;
    comp->mainBC->thread_names[idx] = strdup(name);
    comp->mainBC->threads[idx] = NULL;
    comp->mainBC->thread_argc[idx] = 0;
    return idx;
}

static int lookup_thread(Compiler *comp, const char *name) {
    for (int i = 0; i < comp->mainBC->thread_count; i++)
        if (strcmp(comp->mainBC->thread_names[i], name) == 0) return i;
    return -1;
}

static int register_mutex(Compiler *comp, const char *name) {
    for (int i = 0; i < comp->mutexCount; i++)
        if (strcmp(comp->mutexes[i].name, name) == 0) return i;
    if (comp->mutexCount >= 64) return 0;
    comp->mutexes[comp->mutexCount].name = strdup(name);
    comp->mutexes[comp->mutexCount].idx = comp->mutexCount;
    return comp->mutexCount++;
}

/* 鍑芥暟鍐呭眬閮ㄥ彉閲忥細杩斿洖瀵勫瓨鍣ㄥ彿锛?1 琛ㄧず璧板叏灞€ */
static int lookup_local(Compiler *comp, const char *name) {
    for (int i = 0; i < comp->localCount; i++)
        if (strcmp(comp->locals[i].name, name) == 0) return comp->locals[i].reg;
    return -1;
}

/* 鍑芥暟鍐呭垎閰嶆柊灞€閮ㄥ瘎瀛樺櫒锛堝寰幆璁℃暟鍣紝閬垮厤閲嶅悕鍐茬獊锛?*/
static int fresh_local(Compiler *comp, const char *hint) {
    return alloc_local(comp, hint);
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
    /* 鍙傛暟缁戝畾涓哄眬閮ㄥ彉閲忥紙瀵勫瓨鍣?1..argc锛?*/
    for (int i = 0; i < stmt->funcDef.paramCount; i++) {
        char pn[256];
        snprintf(pn, sizeof(pn), "%.*s", (int)stmt->funcDef.params[i].length, stmt->funcDef.params[i].start);
        alloc_local(comp, pn);
    }
    /* 缂栬瘧鍑芥暟浣?*/
    int *breaks = NULL; int bcount = 0;
    for (int i = 0; i < stmt->funcDef.bodyCount; i++)
        compile_stmt(comp, stmt->funcDef.body[i], &breaks, &bcount);
    free(breaks);
    /* 鏈熬榛樿杩斿洖 nil */
    emit(comp->curBC, OP_RETURN, 0, 0, 0);

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
    /* 鍙傛暟缁戝畾涓哄眬閮ㄥ彉閲忥紙瀵勫瓨鍣?1..argc锛?*/
    for (int i = 0; i < stmt->threadDef.paramCount; i++) {
        char pn[256];
        snprintf(pn, sizeof(pn), "%.*s", (int)stmt->threadDef.params[i].length, stmt->threadDef.params[i].start);
        alloc_local(comp, pn);
    }
    int *breaks = NULL; int bcount = 0;
    for (int i = 0; i < stmt->threadDef.bodyCount; i++)
        compile_stmt(comp, stmt->threadDef.body[i], &breaks, &bcount);
    free(breaks);
    /* 绾跨▼浣撴湯灏撅細HALT 琛ㄧず璇ョ嚎绋嬫墽琛屽畬姣?*/
    emit(comp->curBC, OP_HALT, 0, 0, 0);
    comp->curBC = comp->mainBC;
    comp->in_function = 0;
    comp->in_thread = 0;
    comp->localCount = 0;
    comp->mainBC->threads[tidx] = tbc;
    comp->mainBC->thread_argc[tidx] = stmt->threadDef.paramCount;
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

/* ---------- 琛ㄨ揪寮忕紪璇?---------- */
static int compile_expr(Compiler *comp, Expr *expr) {
    if (!expr) return -1;
    switch (expr->type) {
        case EXPR_NUMBER: {
            int r = alloc_reg();
            emit(comp->curBC, OP_LOADK_INT, r, (int)expr->intVal, 0);
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
            char buf[256];
            snprintf(buf, sizeof(buf), "%.*s", (int)expr->stringVal.length, expr->stringVal.start);
            int idx = bytecode_add_string(comp->curBC, buf);
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
            if (local >= 0) { comp->last_temp = 0; return local; }  /* 灞€閮ㄥ彉閲忥細鎸佷箙瀵勫瓨鍣紝涓嶅彲瑕嗙洊 */
            int r = alloc_reg();
            int g_idx = lookup_global_idx(comp, name);  /* 璇诲彇涓嶅垱寤哄叏灞€锛氭湭瀹氫箟 -> -1 -> NIL */
            emit(comp->curBC, OP_LOAD_GLOBAL, r, g_idx, 0);
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

            /* 瀛楃涓插瓧闈㈤噺姣旇緝蹇€熻矾寰勶細EQK/NEQK锛堝父閲忕洿鎺ョ紪鐮侊紝鐪?LOADK_STRING锛?*/
            if ((expr->binary.op == TOK_EQEQ || expr->binary.op == TOK_NEQ) &&
                expr->binary.left && expr->binary.right &&
                (expr->binary.left->type == EXPR_STRING || expr->binary.right->type == EXPR_STRING)) {
                Expr *lit = (expr->binary.right->type == EXPR_STRING) ? expr->binary.right : expr->binary.left;
                Expr *other = (lit == expr->binary.right) ? expr->binary.left : expr->binary.right;
                int lr = compile_expr(comp, other);
                int l_temp = comp->last_temp;
                char buf[256];
                snprintf(buf, sizeof(buf), "%.*s", (int)lit->stringVal.length, lit->stringVal.start);
                int sidx = bytecode_add_string(comp->curBC, buf);
                int result;
                if (l_temp) result = lr;
                else result = alloc_reg();
                emit(comp->curBC, (expr->binary.op == TOK_EQEQ) ? OP_EQK : OP_NEQK, result, lr, sidx);
                comp->last_temp = 1;
                return result;
            }

            OpCode op;
            switch (expr->binary.op) {
                case TOK_PLUS:  op = OP_ADD; break;
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
            /* 涓存椂鍙橀噺浼樺寲锛氱粨鏋滃鐢ㄥ乏鎿嶄綔鏁板瘎瀛樺櫒锛堣嫢宸︽槸涓存椂锛夛紝骞跺洖鏀跺彸瀛愭爲涓存椂瀵勫瓨鍣?*/
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
            if (expr->call.callee->type == EXPR_IDENT || expr->call.callee->type == EXPR_MEMBER) {
                char fname[256];
                if (expr->call.callee->type == EXPR_IDENT) {
                    snprintf(fname, sizeof(fname), "%.*s", (int)expr->call.callee->identName.length,
                             expr->call.callee->identName.start);
                } else {
                    /* 鍛藉悕绌洪棿璋冪敤锛歶.add(...) -> 鎷兼帴涓?"u.add" */
                    Expr *obj = expr->call.callee->member.object;
                    if (obj->type != EXPR_IDENT) return -1;
                    snprintf(fname, sizeof(fname), "%.*s.%.*s",
                             (int)obj->identName.length, obj->identName.start,
                             (int)expr->call.callee->member.member.length, expr->call.callee->member.member.start);
                }
                int w0 = next_register;
                /* 鑷畾涔夊嚱鏁颁紭鍏堬紙缂栬瘧鏈熺洿鎺ョ储寮曪紝杩愯鏃堕浂鏌ユ壘锛?*/
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
            } else {
                r = alloc_reg();
                if (expr->unary.op == TOK_NOT) emit(comp->curBC, OP_NOT, r, operand, 0);
                else if (expr->unary.op == TOK_MINUS) emit(comp->curBC, OP_NEG, r, operand, 0);
                else emit(comp->curBC, OP_MOV, r, operand, 0); /* 涓€鍏?+ */
            }
            comp->last_temp = 1;
            return r;
        }
        case EXPR_LIST: {
            /* 姣忎釜鍏冪礌鍏堝帇鏍堬紝NEW_ARRAY 浠庢爤鏀堕泦锛堟敮鎸佸祵濂楁暟缁?浠绘剰琛ㄨ揪寮忥級 */
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
            /* 閿€煎浜ゆ浛鍘嬫爤锛孫P_NEW_DICT 浠庢爤鍙?2*count 涓?*/
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
                /* 鍛藉悕绌洪棿鍙橀噺锛歶.count -> 鎷兼帴涓?"u.count" */
                char full[512];
                snprintf(full, sizeof(full), "%s.%s", obj, mem);
                int local = lookup_local(comp, full);
                if (local >= 0) { comp->last_temp = 0; return local; }
                int r = alloc_reg();
                int g_idx = lookup_global_idx(comp, full);  /* 璇诲彇涓嶅垱寤哄叏灞€ */
                emit(comp->curBC, OP_LOAD_GLOBAL, r, g_idx, 0);
                comp->last_temp = 1;
                return r;
            }
            return -1;
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

/* 閾惧紡绱㈠紩璧嬪€?a[i][j]...[k] = v锛氫腑闂村眰涓?nil 鏃惰嚜鍔ㄥ垱寤烘暟缁?*/
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
        emit(comp->curBC, OP_JUMP_IF_FALSE, isn, 0, 0); /* 闈?nil 璺宠繃鍒涘缓 */
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
                else kind = 3; /* inst */
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
                if (br->mode == 0) {
                    /* value list: any pattern == subject -> body */
                    for (int pi = 0; pi < br->patternCount; pi++) {
                        int pat = compile_expr(comp, br->patterns[pi]);
                        int tmp = alloc_reg();
                        emit(comp->curBC, OP_EQ, tmp, subj, pat);
                        int jt = comp->curBC->count;
                        emit(comp->curBC, OP_JUMP_IF_TRUE, tmp, 0, 0);
                        add_break(&body_jumps, &body_jcount, jt);
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
                int body_start = comp->curBC->count;
                for (int i = 0; i < br->bodyCount; i++)
                    compile_stmt(comp, br->body[i], break_list, break_count_ptr);
                int je = comp->curBC->count;
                emit(comp->curBC, OP_JUMP, 0, 0, 0);
                add_break(&end_jumps, &end_count, je);
                for (int i = 0; i < body_jcount; i++)
                    comp->curBC->code[body_jumps[i]].r2 = body_start;
                for (int i = 0; i < skip_count; i++)
                    comp->curBC->code[skip_jumps[i]].r2 = comp->curBC->count; /* next branch */
                free(body_jumps); free(skip_jumps);
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
        case STMT_INCLUDE: case STMT_IMPORT:
            break;

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
                free(breaks);
                emit(comp->curBC, OP_JUMP, 0, start, 0);
                comp->curBC->code[jf].r2 = comp->curBC->count;
                release_temps(comp);
                break;
            }
            if (strcmp(verb, "when") == 0) {
                if (stmt->guiStmt.argCount >= 1 && stmt->guiStmt.args[0]->type == EXPR_IDENT) {
                    /* when flag { } 鈫?涓昏剼鏈? body 缂栬瘧杩涘綋鍓嶄綔鐢ㄥ煙 */
                    int *breaks = NULL; int bcount = 0;
                    for (int i = 0; i < stmt->guiStmt.bodyCount; i++)
                        compile_stmt(comp, stmt->guiStmt.body[i], &breaks, &bcount);
                    free(breaks);
                } else {
                    /* when "msg" { } 鈫?骞挎挱绾跨▼: 寰幆绛夊緟骞挎挱鍚庢墽琛?body */
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
                        /* 涓荤▼搴忓惎鍔ㄧ簿鐏电嚎绋?*/
                        emit(comp->curBC, OP_THREAD_START, tidx, alloc_reg(), 0);
                        Bytecode *tbc = malloc(sizeof(Bytecode)); bytecode_init(tbc);
                        Bytecode *saved = comp->curBC;
                        comp->curBC = tbc;
                        comp->in_function = 1; comp->in_thread = 1;
                        comp->localCount = 0; comp->gdeclCount = 0; comp->local_peak = 0;
                        reset_regs();
                        /* 绾跨▼浣撳紑澶? gui_bind(绮剧伒鍚? */
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
            /* 鍏朵粬 verb 鈫?CALL_BUILTIN "gui_<verb>", args */
            {
                /* 绮剧伒鍚嶅弬鏁颁綅缃?ident 缂栬瘧涓哄瓧绗︿覆甯搁噺) */
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
            /* 浠呰褰曞０鏄庯紙鍑芥暟鍐呭啓鍏ㄥ眬鐨勬樉寮忔巿鏉冿級锛屼笉鍙戝皠鎸囦护 */
            for (int i = 0; i < stmt->globalStmt.nameCount; i++) {
                char gname[256];
                snprintf(gname, sizeof(gname), "%.*s", (int)stmt->globalStmt.names[i].length, stmt->globalStmt.names[i].start);
                if (comp->gdeclCount < 128 && lookup_global_decl(comp, gname) < 0)
                    comp->gdecl[comp->gdeclCount++] = strdup(gname);
            }
            break;
        }
        case STMT_MAIN: {
            /* main{} 鐨?body 灞曞紑缂栬瘧杩涗富绋嬪簭锛堜富绾跨▼鍏ュ彛锛?*/
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
                /* worker.wait N锛氭殏鍋滅洰鏍囩嚎绋?N 绉掞紙VM 瀹氭椂鎭㈠锛?*/
                int sec_reg = compile_expr(comp, stmt->threadWaitStmt.arg);
                emit(comp->curBC, OP_THREAD_WAIT, tidx, sec_reg, 0);
            } else {
                /* worker.wait until cond[, timeout]锛氱紪璇戜负杞寰幆锛屾潯浠朵负鐪熸垨瓒呮椂鍚?resume */
                int jout = -1;
                int t_reg = -1;
                if (stmt->threadWaitStmt.timeout) {
                    int base = compile_expr(comp, stmt->threadWaitStmt.timeout);
                    t_reg = alloc_reg();
                    int hundred = alloc_reg(); emit(comp->curBC, OP_LOADK_INT, hundred, 100, 0);
                    emit(comp->curBC, OP_MUL, t_reg, base, hundred);  /* 杩戜技 100 娆?绉?*/
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
            // window(width, height, title) -> 缂栬瘧涓哄唴缃嚱鏁拌皟鐢?
            if (stmt->windowStmt.width) compile_expr(comp, stmt->windowStmt.width);
            else { int r = alloc_reg(); emit(comp->curBC, OP_LOADK_INT, r, 800, 0); }
            if (stmt->windowStmt.height) compile_expr(comp, stmt->windowStmt.height);
            else { int r = alloc_reg(); emit(comp->curBC, OP_LOADK_INT, r, 600, 0); }
            if (stmt->windowStmt.title) compile_expr(comp, stmt->windowStmt.title);
            else { int r = alloc_reg(); int idx = bytecode_add_string(comp->curBC, "Inimerse"); emit(comp->curBC, OP_LOADK_STRING, r, idx, 0); }

            int builtin_idx = lookup_builtin(comp, "window");
            if (builtin_idx >= 0) {
                emit(comp->curBC, OP_PUSH_REG, alloc_reg() - 3, 0, 0); // 鏍囬瀵勫瓨鍣?
                emit(comp->curBC, OP_PUSH_REG, alloc_reg() - 2, 0, 0); // 楂樺害瀵勫瓨鍣?
                emit(comp->curBC, OP_PUSH_REG, alloc_reg() - 1, 0, 0); // 瀹藉害瀵勫瓨鍣?
                int result = alloc_reg();
                emit(comp->curBC, OP_CALL_BUILTIN, result, builtin_idx, 3);
            }
            release_temps(comp);
            break;
        }

        case STMT_SAY: {
            int r = compile_expr(comp, stmt->sayStmt.message);
            if (comp->in_thread) {
                /* 绮剧伒绾跨▼鍐? say 鈫?姘旀场(褰撳墠缁戝畾绮剧伒) */
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

        case STMT_ASSIGN: {

    /* const check: assigning to a const global is a compile error (top-level only) */
    if (!comp->in_function && stmt->assignStmt.target && stmt->assignStmt.target->type == EXPR_IDENT) {
        char nm[256];
        snprintf(nm, sizeof(nm), "%.*s", (int)stmt->assignStmt.target->identName.length, stmt->assignStmt.target->identName.start);
        int g = register_global(comp, nm);
        if (comp_const_is(comp, g)) {
            fprintf(stderr, "Error: cannot assign to const '%s'\n", nm);
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
                    comp_record_mark(comp, g);
                    break;
                }
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
                    /* 灞€閮ㄥ彉閲忥紙鍑芥暟鍐咃級锛氱洿鎺ュ啓瀵勫瓨鍣?*/
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
            free(my_breaks);
            release_temps(comp);
            break;
        }

        case STMT_FOR: {
            if (stmt->forStmt.iterExpr) {
                /* for x in <鏁扮粍>锛歩=0; while i<len(arr): x=arr[i]; body; i++ */
                int *my_breaks = NULL; int my_bc = 0;
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
                protect_reg(comp, r_i);  /* 寰幆绱㈠紩璺ㄥ惊鐜綋瀛樻椿锛堣鍙ョ骇閲婃斁浼氬洖鏀朵复鏃跺瘎瀛樺櫒锛?*/
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
                release_temps(comp);  /* 寰幆浣撹鍙ラ棿鐨勪复鏃跺洖鏀?*/
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
                free(my_breaks);
                break;
            }
            char var[256];
            snprintf(var, sizeof(var), "%.*s", (int)stmt->forStmt.var.length, stmt->forStmt.var.start);
            int local = lookup_local(comp, var);
            int idx = -1;   /* 浠呬富绋嬪簭浣跨敤鍏ㄥ眬妲?*/
            if (local >= 0) {
                if (stmt->forStmt.rangeStart) {
                    int r = compile_expr(comp, stmt->forStmt.rangeStart);
                    emit(comp->curBC, OP_MOV, local, r, 0);
                } else {
                    emit(comp->curBC, OP_LOADK_INT, local, 0, 0);
                }
            } else if (comp->in_function) {
                /* 鍑芥暟鍐呭惊鐜彉閲忥細global 澹版槑鍒欏啓鍏ㄥ眬锛屽惁鍒欏眬閮?*/
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
            int *my_breaks = NULL; int my_bc = 0;
            int loopStart = comp->curBC->count;
            int reg_idx;
            if (local >= 0) reg_idx = local;
            else { reg_idx = alloc_reg(); emit(comp->curBC, OP_LOAD_GLOBAL, reg_idx, idx, 0); }
            int end_reg = compile_expr(comp, stmt->forStmt.rangeEnd);
            protect_reg(comp, end_reg);  /* 缁撴潫鍊艰法寰幆浣撳瓨娲?*/
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
            free(my_breaks);
            release_temps(comp);
            break;
        }

        case STMT_REPEAT: {
            int *my_breaks = NULL; int my_bc = 0;
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
            int r_max = compile_expr(comp, stmt->repeatStmt.count);
            int cmp = alloc_reg();
            emit(comp->curBC, OP_LT, cmp, r_cnt, r_max);
            int jout = comp->curBC->count;
            emit(comp->curBC, OP_JUMP_IF_FALSE, cmp, 0, 0);
            for (int i = 0; i < stmt->repeatStmt.bodyCount; i++)
                compile_stmt(comp, stmt->repeatStmt.body[i], &my_breaks, &my_bc);
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
            free(my_breaks);
            break;
        }

        case STMT_DO_UNTIL: {
            int *my_breaks = NULL; int my_bc = 0;
            int loopStart = comp->curBC->count;
            for (int i = 0; i < stmt->doUntilStmt.bodyCount; i++)
                compile_stmt(comp, stmt->doUntilStmt.body[i], &my_breaks, &my_bc);
            int cond = compile_expr(comp, stmt->doUntilStmt.condition);
            emit(comp->curBC, OP_JUMP_IF_FALSE, cond, loopStart, 0);
            int exit = comp->curBC->count;
            for (int i = 0; i < my_bc; i++)
                comp->curBC->code[my_breaks[i]].r2 = exit;
            free(my_breaks);
            break;
        }

        case STMT_WAIT_UNTIL: {
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
            free(my_breaks);
            break;
        }

        case STMT_BREAK: {
            int pos = comp->curBC->count;
            emit(comp->curBC, OP_JUMP, 0, 0, 0);
            add_break(break_list, break_count_ptr, pos);
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

/* ---------- 缂栬瘧鍣ㄦ帴鍙?---------- */
Compiler *compiler_new(void) {
    Compiler *comp = malloc(sizeof(Compiler));
    memset(comp, 0, sizeof(Compiler)); /* ensure tag_stack/tag_depth/record_flags/const_flags/gdecl are zeroed */
    comp->mainBC = malloc(sizeof(Bytecode));
    bytecode_init(comp->mainBC);
    comp->curBC = comp->mainBC;
    comp->globals = NULL; comp->globalCount = 0; comp->globalCap = 0;
    comp->localCount = 0;
    comp->in_function = 0;
    comp->builtinCount = 0;
    comp->builtins[comp->builtinCount++].name = strdup("random");
    comp->builtins[comp->builtinCount++].name = strdup("sqrt");
    comp->builtins[comp->builtinCount++].name = strdup("read_file");
    comp->builtins[comp->builtinCount++].name = strdup("write_file");
    comp->builtins[comp->builtinCount++].name = strdup("input");
    comp->builtins[comp->builtinCount++].name = strdup("int");
    comp->builtins[comp->builtinCount++].name = strdup("float");
    comp->builtins[comp->builtinCount++].name = strdup("str");
    comp->builtins[comp->builtinCount++].name = strdup("bool");
    comp->builtins[comp->builtinCount++].name = strdup("len");
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
    free(comp);
}

void compiler_compile(Compiler *comp, Program *prog) {
    /* 绗竴閬嶏細鏀堕泦鎵€鏈夊嚱鏁板悕涓庣嚎绋嬪悕锛堟敮鎸佸墠鍚戝紩鐢?閫掑綊锛?*/
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
        }
    }
    /* 绗簩閬嶏細缂栬瘧涓荤▼搴忥紙璺宠繃鍑芥暟/绾跨▼瀹氫箟锛沵ain{} 鐨?body 鍦?compile_stmt 涓睍寮€锛?*/
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
    /* 娓告垙妯″紡(鏈?on 绮剧伒绾跨▼): 涓荤嚎绋嬫敞鍏ョ瓑寰呭惊鐜?淇濇寔绮剧伒绾跨▼杩愯 */
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
    /* 绗笁閬嶏細缂栬瘧鍚勫嚱鏁颁綋 */
    for (int i = 0; i < prog->count; i++) {
        if (prog->stmts[i]->type == STMT_FUNC)
            compile_func_body(comp, prog->stmts[i]);
    }
    /* 绗洓閬嶏細缂栬瘧鍚勭嚎绋嬩綋 */
    for (int i = 0; i < prog->count; i++) {
        if (prog->stmts[i]->type == STMT_THREAD_DEF)
            compile_thread_body(comp, prog->stmts[i]);
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
}
