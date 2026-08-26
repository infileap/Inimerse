#ifndef COMPILER_H
#define COMPILER_H

#include "ast.h"
#include "bytecode.h"

typedef struct {
    char *name;
    int index;
} GlobalVar;

typedef struct {
    char *name;
} BuiltinInfo;

typedef struct { char *name; int start_off; int end_off; } LabelDef;
typedef struct { int jump_pos; char *label; int kind; } LabelPatch; /* kind 0=to(start) 1=break(end) 2=thread-goto */

typedef struct Compiler {
    Bytecode *mainBC;
    Bytecode *curBC;            /* 褰撳墠鍙戝皠鐩爣锛堜富绋嬪簭鎴栧嚱鏁颁綋鎴栫嚎绋嬩綋锛?*/
    GlobalVar *globals;
    int globalCount, globalCap;
    BuiltinInfo builtins[64];
    int builtinCount;
    /* 鍑芥暟鍐呭眬閮ㄥ彉閲忥細鍚嶅瓧 鈫?瀵勫瓨鍣ㄥ彿 */
    struct { char *name; int reg; } locals[1024];
    int localCount;
    int in_function;            /* 姝ｅ湪缂栬瘧鍑芥暟浣?绾跨▼浣擄紙灞€閮ㄥ彉閲忔ā寮忥級 */
    /* using 妯＄粍鍚嶆敹闆嗭紙鎵撳寘鏃剁敤浜庡祵鍏ュ搴旀ā缁勶級 */
    char **usingMods;
    int usingCount;
    /* 绾跨▼琛細鍚嶅瓧 鈫?瀛楄妭鐮佹绱㈠紩 */
    struct { char *name; int idx; } threads[64];
    int threadCount;
    /* 浜掓枼閿佽〃锛氬悕瀛?鈫?閿佺储寮?*/
    struct { char *name; int idx; } mutexes[256];
    int mutexCount;
    /* 绾跨▼浣撳弬鏁颁釜鏁帮紙缂栬瘧绾跨▼浣撴椂鐢級 */
    int cur_thread_argc;
    int in_thread;
    /* record: tag context stack & registration flags */
    RecordTag **tag_stack;
    int *tag_stack_count;
    int tag_depth;
    int *record_flags;
    int record_flags_cap;
 int *const_flags; /* const globals (by global index) */
 int const_flags_cap;
    /* 瀵勫瓨鍣ㄦ按浣嶏細local_peak=宸插懡鍚嶅眬閮ㄥ彉閲忕殑鏈€澶ф按浣嶏紙鎸佷箙锛屼笉鍙噴鏀撅級 */
    int local_peak;
    int last_temp;          /* 鏈€杩戜竴娆?compile_expr 鐨勭粨鏋滃瘎瀛樺櫒鏄惁涓存椂锛堝彲瑕嗙洊锛?*/
    /* 鍑芥暟鍐?global 澹版槑鍒楄〃锛氭樉寮忓０鏄庡悗鍐欏叏灞€锛圥ython 寮忥紱鏈０鏄庤祴鍊?灞€閮級 */
    char *gdecl[1024];
    int gdeclCount;
    /* continue targets (current loop) */
    int **cont_list;
    int *cont_count;
    /* label block system */
    LabelDef *labels;
    int labelCount;
    LabelPatch *patches;
    int patchCount;

    /* import/include 编译期解析（命名空间前缀栈，取代 parser 拼接+rename） */
    char cur_ns[512];      /* 当前命名空间前缀："m." / "outer.s."；"" = 根 */
    char cur_dir[1024];    /* 当前文件目录（相对 import 路径基准），"" = CWD */
    char **ns_visible;     /* 当前模块内可见的命名空间相对名（import as 声明） */
    int ns_visible_count;
    int ns_visible_cap;
    struct ImportEntry { char *key; int state; Program *prog; } *imports; /* state: 0=进行中 1=已收集 2=已编译 */
    int import_count;
    int import_cap;
    int import_depth;      /* 递归深度防护 */
} Compiler;

Compiler *compiler_new(void);
int register_global(Compiler *comp, const char *name);
void compiler_free(Compiler *comp);
void compiler_compile(Compiler *comp, Program *prog);
Bytecode *compiler_get_main_bytecode(Compiler *comp);

/* 杩斿洖鑴氭湰涓?using 鐨勬ā缁勫悕锛堥€楀彿鍒嗛殧锛宮alloc锛岃皟鐢ㄨ€?free锛夛紝鏃犲垯杩斿洖 NULL */
char *compiler_get_using_mods(Compiler *comp);

#endif
