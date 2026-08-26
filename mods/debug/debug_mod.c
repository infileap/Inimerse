/*
 * debug ģ�� v2 ���� ָ��߽������
 *
 * ԭ��:VM ��ÿ��ָ��ִ��ǰ�ı߽��� step_mode(�����߳�),
 * ��λʱͬ�� vm->ip ���ص� vm->debug_hook(��ģ�鰲װ)���ص�����
 * ������ʾ,���غ����ִ�С���˵���/�ϵ㲻��ʧ�Ĵ�����ջ�����֡״̬
 * (�ɰ����� vm_step ����ʽ"����",ÿ�� vm_run ���� ip=0 ���¿�ʼ,ʵ�ʲ�����)��
 *
 * �÷�:
 *   inimerse.exe debug script.im       # ͣ�ڵ�һ��ָ��ǰ���뽻��
 *   �ű��ڵ��� debug_break()              # ���е��˴�ͣ��
 *   �ű��ڵ��� debug_traceback()          # ��ӡ��ǰ����ջ�����
 *
 * ��������:
 *   s            ����(ִ��һ��ָ��)
 *   n            ����(������������,ͣ�ڵ��÷��غ����һ��ָ��)
 *   fin          ����(ִ�е���ǰ��������)
 *   u <ip>       ���е���ǰ�����ָ��ƫ��(�ں�����Ϊ������ƫ��)
 *   c            ����(�޶ϵ�ֱ������;�жϵ�����ִ�����ϵ�)
 *   b [ip]       ��ϵ�(Ĭ�ϵ�ǰָ��);b f <������> ������ڶϵ�
 *   bl / bc [n]  �г��ϵ� / ����ϵ�(��������=ȫ��)
 *   p [����...]  �鿴ȫ�ֱ���(�ɶ��);p r<N> �鿴��ǰ֡�Ĵ���
 *   p            �г�ȫ��ȫ�ֱ���
 *   r [n]        ��ӡ��ǰ֡ǰ n ���Ĵ���(Ĭ��16,�����ڱ�ע [arg]/[ret])
 *   bt           ����ջ
 *   t            ���߳�״̬
 *   dis [ip [n]] ����൱ǰ�����(Ĭ�ϴӵ�ǰָ����10��)
 *   src [a [b]]  �鿴Դ����
 *   w <����>     ��Ӽ���(ÿ��ͣ��ʱ��ӡ);wl �б�;wd <n> ɾ��
 *   q            ��ֹ����(ֹͣ�����߳�)
 *   h            ����
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "vm.h"
#include "parser.h"
#include "compiler.h"
#include "bytecode.h"

/* ============ �Ự״̬(�����̷߳���) ============ */

typedef struct { Bytecode *code; int ip; } DbgBP;

#define MAX_BP    256
#define MAX_WATCH 64
#define MAX_WNAME 64

static VM      *g_vm;
static Compiler *g_comp;   /* ��������Ϣ:ȫ������ */
static Bytecode *g_root;   /* ���ֽ���(����/�̱߳�) */
static char    *g_source;  /* �ű�Դ��(src ������) */

static DbgBP    g_bps[MAX_BP];
static int      g_bp_count;
static char     g_watch[MAX_WATCH][MAX_WNAME];
static int      g_watch_count;

/* ����ģʽ:0=ͣ����ʾ�� 1=���� 2=���� 3=���е� 4=�ϵ���� */
static int g_mode;
static int g_mode_depth;
static Bytecode *g_mode_code;
static int g_mode_ip;
static int g_stop_no;

/* ============ ���� ============ */

static const char *code_name(Bytecode *code, int *is_func, int *fidx, int *tidx) {
    if (is_func) *is_func = 0;
    if (fidx) *fidx = -1;
    if (tidx) *tidx = -1;
    if (!g_root) return "?";
    if (code == g_root) return "main";
    if (is_func) *is_func = 1;
    if (g_root->funcs) {
        for (int i = 0; i < g_root->func_count && i < 64; i++) {
            if (g_root->funcs[i] == code) {
                if (fidx) *fidx = i;
                return g_root->func_names[i] ? g_root->func_names[i] : "<func>";
            }
        }
    }
    if (g_root->threads) {
        for (int i = 0; i < g_root->thread_count && i < 32; i++) {
            if (g_root->threads[i] == code) {
                if (tidx) *tidx = i;
                return g_root->thread_names[i] ? g_root->thread_names[i] : "<thread>";
            }
        }
    }
    return "?";
}

static const char *global_name(int idx) {
    if (g_comp && idx >= 0 && idx < g_comp->globalCount && g_comp->globals[idx].name)
        return g_comp->globals[idx].name;
    return NULL;
}

static void disasm_ins(Bytecode *code, int ip, char *out, size_t outsz) {
    RegInstruction *ins = &code->code[ip];
    const char *gn, *sn;
    switch (ins->op) {
    case OP_MOV:            snprintf(out, outsz, "MOV r%d, r%d", ins->r1, ins->r2); return;
    case OP_LOADK_INT:      snprintf(out, outsz, "LOADK_INT r%d, %d", ins->r1, ins->r2); return;
    case OP_LOADK_FLOAT:
        snprintf(out, outsz, "LOADK_FLOAT r%d, #%d (%g)", ins->r1, ins->r2,
                 (ins->r2 >= 0 && ins->r2 < code->float_count) ? code->float_pool[ins->r2] : 0.0);
        return;
    case OP_LOADK_STRING:
        snprintf(out, outsz, "LOADK_STRING r%d, #%d (\"%s\")", ins->r1, ins->r2,
                 (ins->r2 >= 0 && ins->r2 < code->string_count && code->string_pool[ins->r2])
                     ? code->string_pool[ins->r2] : "?");
        return;
    case OP_LOADK_BOOL:     snprintf(out, outsz, "LOADK_BOOL r%d, %s", ins->r1, ins->r2 ? "true" : "false"); return;
    case OP_ADD:            snprintf(out, outsz, "ADD r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_CONCAT:        snprintf(out, outsz, "CONCAT r%d, r%d..r%d (n=%d)", ins->r1, ins->r2, ins->r2 + ins->r3 - 1, ins->r3); return;

    case OP_SUB:            snprintf(out, outsz, "SUB r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_MUL:            snprintf(out, outsz, "MUL r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_DIV:            snprintf(out, outsz, "DIV r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_NEG:            snprintf(out, outsz, "NEG r%d, r%d", ins->r1, ins->r2); return;
    case OP_EQ:             snprintf(out, outsz, "EQ r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_NEQ:            snprintf(out, outsz, "NEQ r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_LT:             snprintf(out, outsz, "LT r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_GT:             snprintf(out, outsz, "GT r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_LE:             snprintf(out, outsz, "LE r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_GE:             snprintf(out, outsz, "GE r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_AND:            snprintf(out, outsz, "AND r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_OR:             snprintf(out, outsz, "OR r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_NOT:            snprintf(out, outsz, "NOT r%d, r%d", ins->r1, ins->r2); return;
    case OP_NEW_ARRAY:      snprintf(out, outsz, "NEW_ARRAY r%d, n=%d (from stack)", ins->r1, ins->r3); return;
    case OP_INDEX_GET:      snprintf(out, outsz, "INDEX_GET r%d, r%d[r%d]", ins->r1, ins->r2, ins->r3); return;
    case OP_INDEX_SET:      snprintf(out, outsz, "INDEX_SET r%d[r%d] = r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_LOAD_GLOBAL:
        gn = global_name(ins->r2);
        if (gn) snprintf(out, outsz, "LOAD_GLOBAL r%d, [%d] (%s)", ins->r1, ins->r2, gn);
        else    snprintf(out, outsz, "LOAD_GLOBAL r%d, [%d]", ins->r1, ins->r2);
        return;
    case OP_STORE_GLOBAL:
        gn = global_name(ins->r1);
        if (gn) snprintf(out, outsz, "STORE_GLOBAL [%d] (%s) = r%d", ins->r1, gn, ins->r2);
        else    snprintf(out, outsz, "STORE_GLOBAL [%d] = r%d", ins->r1, ins->r2);
        return;
    case OP_JUMP:           snprintf(out, outsz, "JUMP %d", ins->r2); return;
    case OP_JUMP_IF_FALSE:  snprintf(out, outsz, "JUMP_IF_FALSE r%d, %d", ins->r1, ins->r2); return;
    case OP_JUMP_IF_TRUE:   snprintf(out, outsz, "JUMP_IF_TRUE r%d, %d", ins->r1, ins->r2); return;
    case OP_CALL_BUILTIN:
        sn = (ins->r2 >= 0 && ins->r2 < code->string_count && code->string_pool[ins->r2])
                 ? code->string_pool[ins->r2] : "?";
        snprintf(out, outsz, "CALL_BUILTIN r%d, %s (args:%d)", ins->r1, sn, ins->r3);
        return;
    case OP_PUSH_REG:       snprintf(out, outsz, "PUSH_REG r%d", ins->r1); return;
    case OP_POP_REG:        snprintf(out, outsz, "POP_REG r%d", ins->r1); return;
    case OP_SAY:            snprintf(out, outsz, "SAY r%d", ins->r1); return;
    case OP_WAIT:           snprintf(out, outsz, "WAIT r%d", ins->r1); return;
    case OP_STOP:           snprintf(out, outsz, "STOP"); return;
    case OP_HALT:           snprintf(out, outsz, "HALT"); return;
    case OP_CALL_FUNC:
        if (g_root && ins->r1 >= 0 && ins->r1 < g_root->func_count && g_root->func_names[ins->r1])
            snprintf(out, outsz, "CALL_FUNC %s (args:%d, res:r%d)", g_root->func_names[ins->r1], ins->r3, ins->r2);
        else
            snprintf(out, outsz, "CALL_FUNC f%d (args:%d, res:r%d)", ins->r1, ins->r3, ins->r2);
        return;
    case OP_RETURN:         snprintf(out, outsz, "RETURN r%d", ins->r1); return;
    case OP_IS_NIL:         snprintf(out, outsz, "IS_NIL r%d, r%d", ins->r1, ins->r2); return;
    case OP_THREAD_START:
        if (g_root && ins->r1 >= 0 && ins->r1 < g_root->thread_count && g_root->thread_names[ins->r1])
            snprintf(out, outsz, "THREAD_START %s (args:%d, res:r%d)", g_root->thread_names[ins->r1], ins->r3, ins->r2);
        else
            snprintf(out, outsz, "THREAD_START t%d (args:%d, res:r%d)", ins->r1, ins->r3, ins->r2);
        return;
    case OP_THREAD_CTRL:    snprintf(out, outsz, "THREAD_CTRL t%d, op=%d", ins->r1, ins->r2); return;
    case OP_THREAD_JOIN:    snprintf(out, outsz, "THREAD_JOIN t%d, timeout=r%d", ins->r1, ins->r2); return;
    case OP_THREAD_WAIT:    snprintf(out, outsz, "THREAD_WAIT t%d, cond=r%d, timeout=r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_THREAD_STATE:   snprintf(out, outsz, "THREAD_STATE r%d, t%d, attr=%d", ins->r1, ins->r2, ins->r3); return;
    case OP_LOCK:           snprintf(out, outsz, "LOCK m%d, %s", ins->r1, ins->r2 ? "unlock" : "lock"); return;
    case OP_SEND:           snprintf(out, outsz, "SEND t%d, msg=r%d", ins->r1, ins->r2); return;
    case OP_RECV:           snprintf(out, outsz, "RECV r%d, timeout=r%d", ins->r1, ins->r2); return;
    case OP_NEW_DICT:       snprintf(out, outsz, "NEW_DICT r%d, pairs=%d", ins->r1, ins->r3); return;
    case OP_EQK:            snprintf(out, outsz, "EQK r%d, r%d, #%d", ins->r1, ins->r2, ins->r3); return;
    case OP_NEQK:           snprintf(out, outsz, "NEQK r%d, r%d, #%d", ins->r1, ins->r2, ins->r3); return;
    default:                snprintf(out, outsz, "OP_%d (r%d,%d,%d)", ins->op, ins->r1, ins->r2, ins->r3); return;
    }
}

static void print_val(VM *vm, const Value *v, char *buf, int bufsz) {
    if (v->type == VAL_STRING) snprintf(buf, bufsz, "\"%s\"", v->sval ? v->sval : "");
    else if (v->type == VAL_OBJECT) snprintf(buf, bufsz, "<object>");
    else vm_value_to_string(vm, v, buf, bufsz);
}

static int find_global(VM *vm, const char *name, Value **out) {
    for (int i = 0; i < vm->globalCount; i++) {
        if (vm->globals[i].name && strcmp(vm->globals[i].name, name) == 0) {
            *out = &vm->globals[i].val;
            return 1;
        }
    }
    if (g_comp) { /* VM runtime globals have NULL names; use compiler name table */
        for (int i = 0; i < g_comp->globalCount; i++) {
            if (g_comp->globals[i].name && strcmp(g_comp->globals[i].name, name) == 0) {
                if (i < vm->globalCount) { *out = &vm->globals[i].val; return 1; }
                return 0;
            }
        }
    }
    return 0;
}

/* ============ �ϵ� ============ */

static int bp_exists(Bytecode *code, int ip) {
    for (int i = 0; i < g_bp_count; i++)
        if (g_bps[i].code == code && g_bps[i].ip == ip) return 1;
    return 0;
}

static void bp_add(Bytecode *code, int ip) {
    if (g_bp_count >= MAX_BP) { printf("too many breakpoints\n"); return; }
    if (bp_exists(code, ip)) { printf("breakpoint already set\n"); return; }
    g_bps[g_bp_count].code = code;
    g_bps[g_bp_count].ip = ip;
    g_bp_count++;
    printf("breakpoint %d set at %s ip=%d\n", g_bp_count, code_name(code, NULL, NULL, NULL), ip);
}

static void bp_list(void) {
    if (!g_bp_count) { printf("(no breakpoints)\n"); return; }
    for (int i = 0; i < g_bp_count; i++)
        printf("  %d: %s ip=%d\n", i + 1, code_name(g_bps[i].code, NULL, NULL, NULL), g_bps[i].ip);
}

static void bp_clear(int idx) { /* idx 1-based; 0=ȫ�� */
    if (idx < 1 || idx > g_bp_count) { g_bp_count = 0; printf("all breakpoints cleared\n"); return; }
    for (int i = idx - 1; i < g_bp_count - 1; i++) g_bps[i] = g_bps[i + 1];
    g_bp_count--;
    printf("breakpoint %d removed\n", idx);
}

static int bp_hit(Bytecode *code, int ip) { return bp_exists(code, ip); }

/* ============ ���� ============ */

static void watch_add(const char *name) {
    if (g_watch_count >= MAX_WATCH) { printf("too many watches\n"); return; }
    for (int i = 0; i < g_watch_count; i++)
        if (strcmp(g_watch[i], name) == 0) { printf("watch exists\n"); return; }
    snprintf(g_watch[g_watch_count], MAX_WNAME, "%s", name);
    g_watch_count++;
    printf("watch %d: %s\n", g_watch_count, name);
}

static void watch_list(void) {
    if (!g_watch_count) { printf("(no watches)\n"); return; }
    for (int i = 0; i < g_watch_count; i++) printf("  %d: %s\n", i + 1, g_watch[i]);
}

static void watch_del(int idx) { /* 1-based */
    if (idx < 1 || idx > g_watch_count) { printf("invalid watch index (1..%d)\n", g_watch_count); return; }
    for (int i = idx - 1; i < g_watch_count - 1; i++) strncpy(g_watch[i], g_watch[i + 1], MAX_WNAME);
    g_watch_count--;
    printf("watch %d removed\n", idx);
}

static void watch_show(VM *vm) {
    for (int i = 0; i < g_watch_count; i++) {
        Value *v = NULL;
        char buf[512];
        if (find_global(vm, g_watch[i], &v)) {
            print_val(vm, v, buf, sizeof buf);
            printf("  watch %s = %s\n", g_watch[i], buf);
        } else {
            printf("  watch %s = (undefined)\n", g_watch[i]);
        }
    }
}

/* ============ ��ʾ ============ */

static void print_backtrace(VM *vm, VmThread *t) {
    (void)vm;
    printf("call stack (%d frames):\n", t->frame_count + 1);
    printf("  #0 %s ip=%d base=%d (current)\n", code_name(t->code, NULL, NULL, NULL), t->ip, t->base);
    for (int k = t->frame_count - 1; k >= 0; k--) {
        printf("  #%d %s ip=%d base=%d (return to %d)\n",
               t->frame_count - k,
               code_name(t->frame_code[k], NULL, NULL, NULL),
               t->frame_ip[k], t->frame_base[k], t->frame_ip[k] - 1);
    }
}

static void print_threads(VM *vm) {
    int n = 0;
    for (int i = 0; i < VM_MAX_THREADS; i++) {
        VmThread *tt = vm->threads[i];
        if (!tt) continue;
        n++;
        const char *st = tt->finished ? "finished"
                       : tt->stop_flag ? "stopping"
                       : tt->paused    ? "paused"
                                       : "running";
        const char *nm = (tt->tidx >= 0 && g_root && tt->tidx < g_root->thread_count && g_root->thread_names[tt->tidx])
                             ? g_root->thread_names[tt->tidx] : "?";
        int q = tt->msg_tail >= tt->msg_head ? tt->msg_tail - tt->msg_head
                                             : tt->msg_tail + tt->msg_cap - tt->msg_head;
        printf("  thread[%d] %s state=%s ip=%d msgs=%d\n", i, nm, st, tt->ip, q);
    }
    if (!n) printf("  (no child threads)\n");
}

static void print_regs(VM *vm, VmThread *t, int n) {
    int is_func = 0, fidx = -1;
    code_name(t->code, &is_func, &fidx, NULL);
    int argc = 0;
    if (is_func && fidx >= 0 && g_root && fidx < g_root->func_count)
        argc = g_root->func_argc[fidx];
    if (n <= 0 || n > 32) n = 16;
    for (int i = 0; i < n; i++) {
        char buf[256];
        if (t->base + i >= VM_THREAD_REG_COUNT) break;
        Value *v = &t->reg[t->base + i];
        if (v->type == VAL_STRING) snprintf(buf, sizeof buf, "\"%s\"", v->sval ? v->sval : "");
        else if (v->type == VAL_OBJECT) snprintf(buf, sizeof buf, "<object>");
        else vm_value_to_string(vm, v, buf, sizeof buf);
        if (is_func && i == 0)       printf("  r%-3d = %-26s [ret]\n", i, buf);
        else if (is_func && i <= argc) printf("  r%-3d = %-26s [arg]\n", i, buf);
        else                         printf("  r%-3d = %s\n", i, buf);
    }
}

static void print_stop(VM *vm, VmThread *t) {
    g_stop_no++;
    char buf[512];
    disasm_ins(t->code, t->ip, buf, sizeof buf);
    const char *fn = code_name(t->code, NULL, NULL, NULL);
    printf("\n== stop #%d [ip:%d] %s depth=%d | %s\n", g_stop_no, t->ip, fn, t->frame_count, buf);
    watch_show(vm);
    printf("dbg> ");
    fflush(stdout);
}

static void print_help(void) {
    printf("commands:\n"
           "  s            step (execute one instruction)\n"
           "  n            step over (skip function call)\n"
           "  fin          step out of current function\n"
           "  u <ip>       run until ip in current code segment\n"
           "  c            continue (free run, or to next breakpoint)\n"
           "  b [ip]       set breakpoint at ip (default: current); b f <name> = function entry\n"
           "  bl           list breakpoints\n"
           "  bc [n]       clear breakpoint n (default: all)\n"
           "  p [name...]  print global variable(s); p r<N> = frame register\n"
           "  p            list all globals\n"
           "  r [n]        dump n frame registers (default 16)\n"
           "  bt           backtrace (call stack)\n"
           "  t            list child threads\n"
           "  dis [ip [n]] disassemble current code segment\n"
           "  src [a [b]]  show source lines\n"
           "  w <name>     add watch; wl list; wd <n> delete\n"
           "  q            quit debugger (terminate all threads)\n"
           "  #run <code>  execute injected code\n"
           "  to <label>   jump main thread to label\n"
           "  thr to lbl   jump thread thr to label\n"
           "  var <mode>   list globals (all/value/type/scope)\n"
           "  #thread      list threads\n"
           "  h            help\n");
}

/* ============ Դ����� ============ */

static void cmd_src(int a, int b) {
    if (!g_source) { printf("(no source)\n"); return; }
    if (a < 1) a = 1;
    int line = 1;
    char *p = g_source;
    while (p && line < a) {
        char *nl = strchr(p, '\n');
        if (!nl) { p = NULL; break; }
        p = nl + 1;
        line++;
    }
    if (!p) { printf("(line %d beyond end)\n", a); return; }
    for (; line <= b && *p; line++) {
        char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        printf("%4d| %.*s\n", line, len, p);
        if (!nl) break;
        p = nl + 1;
    }
}

/* ============ �˳� ============ */

static void quit_debug(VM *vm) {
    vm->step_mode = false;
    g_mode = 0;
    for (int i = 0; i < VM_MAX_THREADS; i++) {
        VmThread *tt = vm->threads[i];
        if (tt) { tt->stop_flag = true; tt->paused = false; }
    }
    VmThread *t = vm_get_cur_thread();
    if (t) t->stop_flag = true;
    printf("[debug] terminated\n");
}

/* ============ ����ѭ�� ============ */

static void interactive(VM *vm, VmThread *t) {
    char line[512];
    for (;;) {
        if (!fgets(line, sizeof line, stdin)) { quit_debug(vm); return; }
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0) { printf("dbg> "); fflush(stdout); continue; }

        char cmd[64] = "", arg[256] = "";
        char *sp = strchr(line, ' ');
        if (sp) {
            size_t cl = (size_t)(sp - line);
            if (cl >= sizeof cmd) cl = sizeof cmd - 1;
            memcpy(cmd, line, cl);
            cmd[cl] = '\0';
            strncpy(arg, sp + 1, sizeof arg - 1);
            arg[sizeof arg - 1] = '\0';
        } else {
            strncpy(cmd, line, sizeof cmd - 1);
            cmd[sizeof cmd - 1] = '\0';
        }
        /* ȥ�� arg β���հ� */
        size_t al = strlen(arg);
        while (al > 0 && (arg[al - 1] == ' ' || arg[al - 1] == '\t')) arg[--al] = '\0';

        if (!strcmp(cmd, "s") || !strcmp(cmd, "step")) {
            vm->step_mode = true;
            return;
        }
        if (!strcmp(cmd, "n") || !strcmp(cmd, "next")) {
            g_mode = 1;
            g_mode_depth = t->frame_count;
            vm->step_mode = true;
            return;
        }
        if (!strcmp(cmd, "fin") || !strcmp(cmd, "out")) {
            g_mode = 2;
            g_mode_depth = t->frame_count;
            vm->step_mode = true;
            return;
        }
        if (!strcmp(cmd, "u") || !strcmp(cmd, "until")) {
            int ip = atoi(arg);
            if (arg[0] == '\0' || ip < 0 || ip >= t->code->count) {
                printf("invalid ip (0..%d)\n", t->code->count - 1);
            } else if (ip < t->ip) {
                printf("target %d is before current ip %d\n", ip, t->ip);
            } else {
                g_mode = 3;
                g_mode_code = t->code;
                g_mode_ip = ip;
                vm->step_mode = true;
                return;
            }
        }
        if (!strcmp(cmd, "c") || !strcmp(cmd, "cont") || !strcmp(cmd, "continue")) {
            if (g_bp_count > 0) { g_mode = 4; vm->step_mode = true; return; }
            return; /* free run */
        }
        if (!strcmp(cmd, "b")) {
            if (arg[0] == '\0') {
                bp_list();
            } else if (!strncmp(arg, "f ", 2)) {
                const char *name = arg + 2;
                int found = 0;
                if (g_root) {
                    for (int i = 0; i < g_root->func_count && i < 64; i++) {
                        if (g_root->func_names[i] && !strcmp(g_root->func_names[i], name)) {
                            bp_add(g_root->funcs[i], 0);
                            found = 1;
                            break;
                        }
                    }
                }
                if (!found) printf("function '%s' not found\n", name);
            } else {
                int ip = atoi(arg);
                if (ip < 0 || ip >= t->code->count) printf("invalid ip (0..%d)\n", t->code->count - 1);
                else bp_add(t->code, ip);
            }
        } else if (!strcmp(cmd, "bl")) {
            bp_list();
        } else if (!strcmp(cmd, "bc")) {
            bp_clear(arg[0] ? atoi(arg) : 0);
        } else if (!strcmp(cmd, "p")) {
            if (arg[0] == '\0') {
                if (!vm->globalCount) printf("(no globals)\n");
                for (int i = 0; i < vm->globalCount; i++) {
                    char buf[512];
                    const char *nm = vm->globals[i].name;
                    if (!nm && g_comp && i < g_comp->globalCount) nm = g_comp->globals[i].name;
                    print_val(vm, &vm->globals[i].val, buf, sizeof buf);
                    printf("  %s = %s\n", nm ? nm : "?", buf);
                }
            } else {
                char *tok = arg;
                while (tok && *tok) {
                    char *sp2 = strchr(tok, ' ');
                    if (sp2) *sp2 = '\0';
                    if (tok[0] == 'r' && tok[1] >= '0' && tok[1] <= '9') {
                        int ri = atoi(tok + 1);
                        if (ri >= 0 && t->base + ri < VM_THREAD_REG_COUNT) {
                            char buf[256];
                            print_val(vm, &t->reg[t->base + ri], buf, sizeof buf);
                            printf("  r%d = %s\n", ri, buf);
                        } else {
                            printf("  r%d out of range\n", ri);
                        }
                    } else if (strcmp(tok, "usage") == 0) {
                        double now = vm->t_start ? (double)(GetTickCount64() - vm->t_start) / 1000.0 : 0.0;
                        printf("usage: mem=%.0f/%.0f threads=%d/%d time=%.2f/%.0fs inst_limit=%.0f\n",
                               vm->used_mem, vm->limit_mem, vm->active_threads, vm->limit_threads,
                               now, vm->limit_time, vm->limit_inst);
                    } else {
                        Value *v = NULL;
                        if (find_global(vm, tok, &v)) {
                            char buf[512];
                            print_val(vm, v, buf, sizeof buf);
                            printf("  %s = %s\n", tok, buf);
                        } else {
                            printf("  '%s' not found (global or r<N>)\n", tok);
                        }
                    }
                    tok = sp2 ? sp2 + 1 : NULL;
                }
            }
        } else if (!strcmp(cmd, "r")) {
            print_regs(vm, t, atoi(arg));
        } else if (!strcmp(cmd, "bt")) {
            print_backtrace(vm, t);
        } else if (!strcmp(cmd, "t")) {
            print_threads(vm);
        } else if (!strcmp(cmd, "dis")) {
            int ip = t->ip, cnt = 10;
            if (arg[0]) {
                ip = atoi(arg);
                char *sp2 = strchr(arg, ' ');
                if (sp2) cnt = atoi(sp2 + 1);
            }
            if (ip < 0 || ip >= t->code->count) {
                printf("invalid ip (0..%d)\n", t->code->count - 1);
            } else {
                if (cnt <= 0) cnt = 10;
                if (ip + cnt > t->code->count) cnt = t->code->count - ip;
                for (int i = ip; i < ip + cnt; i++) {
                    char buf[300];
                    disasm_ins(t->code, i, buf, sizeof buf);
                    printf("%s%4d: %s\n", i == t->ip ? ">" : " ", i, buf);
                }
            }
        } else if (!strcmp(cmd, "src")) {
            int a = 1, b = 40;
            if (arg[0]) {
                a = atoi(arg);
                char *sp2 = strchr(arg, ' ');
                b = sp2 ? atoi(sp2 + 1) : a + 15;
            }
            if (b < a) b = a;
            cmd_src(a, b);
        } else if (!strcmp(cmd, "w")) {
            if (arg[0]) watch_add(arg);
            else watch_list();
        } else if (!strcmp(cmd, "wl")) {
            watch_list();
        } else if (!strcmp(cmd, "wd")) {
            if (arg[0]) watch_del(atoi(arg));
            else printf("usage: wd <n>\n");
        } else if (!strcmp(cmd, "q") || !strcmp(cmd, "quit") || !strcmp(cmd, "exit")) {
            quit_debug(vm);
            return;
        } else if (!strncmp(line, "#run ", 5) || !strncmp(line, "run ", 4)) {
            /* #run <code>: compile & execute injected code */
            const char *code = strchr(line, ' ') + 1;
            vm_debug_exec(vm, code);
        } else if (!strcmp(cmd, "to")) {
            /* to <label>: jump main thread; thread1 to A: parsed below */
            vm_debug_jump(vm, "", arg);
        } else if (strstr(line, " to ")) {
            /* thread1 to A */
            char tname[64] = ""; const char *to = strstr(line, " to ");
            size_t tl = (size_t)(to - line); if (tl >= sizeof tname) tl = sizeof tname - 1;
            memcpy(tname, line, tl); tname[tl] = '\0';
            vm_debug_jump(vm, tname, to + 4);
        } else if (!strcmp(cmd, "var")) {
            /* var all|value|type|scope */
            vm_debug_var(vm, arg);
        } else if (!strcmp(cmd, "#thread") || !strcmp(cmd, "threads")) {
            vm_debug_threads(vm);
        } else if (!strcmp(cmd, "h") || !strcmp(cmd, "help") || !strcmp(cmd, "?")) {
            print_help();
        } else {
            printf("unknown command '%s' (h for help)\n", cmd);
        }
        fflush(stdout);
    }
}

/* ============ ���Թ���(ָ��߽�ص�,�����߳�) ============ */

static void dbg_hook(VM *vm) {
    VmThread *t = vm_get_cur_thread();
    if (!t) return;
    int stop = 0;
    switch (g_mode) {
    case 1: /* ����:��Ȼص�����ǰ */
        if (t->frame_count <= g_mode_depth) stop = 1;
        else { vm->step_mode = true; return; }
        break;
    case 2: /* ����:���С�ڵ���ʱ */
        if (g_mode_depth <= 0 || t->frame_count < g_mode_depth) stop = 1;
        else { vm->step_mode = true; return; }
        break;
    case 3: /* ���е� */
        if (t->code == g_mode_code && t->ip == g_mode_ip) stop = 1;
        else { vm->step_mode = true; return; }
        break;
    case 4: /* �ϵ���� */
        if (bp_hit(t->code, t->ip)) stop = 1;
        else { vm->step_mode = true; return; }
        break;
    default:
        stop = 1;
    }
    if (stop) {
        g_mode = 0;
        print_stop(vm, t);
        interactive(vm, t);
    }
}

/* ============ ������� ============ */

static void debug_script_impl(VM *vm, const char *script_path) {
    FILE *f = fopen(script_path, "rb");
    if (!f) { printf("cannot read script '%s'\n", script_path); return; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *source = malloc(len + 1);
    fread(source, 1, len, f);
    source[len] = '\0';
    fclose(f);

    Program *prog = parse_program(source);
    if (!prog) { printf("parse failed\n"); free(source); return; }
    Compiler *comp = compiler_new();
    compiler_compile(comp, prog);
    Bytecode *bc = compiler_get_main_bytecode(comp);

    /* ���ûỰ״̬ */
    g_vm = vm;
    g_comp = comp;
    g_root = bc;
    free(g_source);
    g_source = strdup(source);
    free(source);
    g_bp_count = 0;
    g_watch_count = 0;
    g_mode = 0;
    g_stop_no = 0;

    vm_load_bytecode(vm, bc);
    printf("[debug] debugging '%s' (%d instructions)\n", script_path, bc->count);
    printf("(h for help, q to quit)\n");
    vm->step_mode = true;
    vm_run(vm);

    printf("[debug] program finished/terminated\n");
    g_vm = NULL;
    g_comp = NULL;
    g_root = NULL;
    free(g_source);
    g_source = NULL;
    compiler_free(comp);
}

/* ============ �ű��ڿɵ��õĵ������� ============ */

static int builtin_debug_break(VM *vm) {
    vm->step_mode = true; /* ��һ��ָ��߽������Թ��� */
    return 0;
}

static int builtin_debug_traceback(VM *vm) {
    VmThread *t = vm_get_cur_thread();
    if (!t) { printf("(no thread)\n"); return 0; }
    print_backtrace(vm, t);
    return 0;
}

__declspec(dllexport) void mod_init(VM *vm) {
    vm->debug_hook = dbg_hook;
    vm->debug_script = debug_script_impl;
    vm_register_builtin(vm, "debug_break", builtin_debug_break);
    vm_register_builtin(vm, "debug_traceback", builtin_debug_traceback);
    g_vm = vm;
    printf("[debug mod] v2 loaded (debug_break / debug_traceback available)\n");
}
