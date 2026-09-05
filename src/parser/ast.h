#ifndef AST_H
#define AST_H

#include "common.h"

/* 线程/主线程标签 (thread/main labels) */
#define THREAD_FLAG_ENDLESS (1 << 0)   /* not killed by the 120s watchdog */
#define THREAD_FLAG_DAEMON  (1 << 1)   /* auto-terminated when main ends */
#define THREAD_FLAG_RESTART (1 << 2)   /* auto-restart when thread finishes naturally */
#define THREAD_FLAG_SINGLE  (1 << 3)   /* only one instance of this thread */
#define THREAD_FLAG_TASK    (1 << 4)   /* virtual thread: runs on a Fiber (no OS thread) */
#include "lexer.h"

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Tag Tag;

typedef struct {
    int mode;                 /* 0=value list, 1=comparison, 2=else */
    InimerseTokenType cmpOp;  /* mode=1 */
    Expr **patterns;          /* mode=0 */
    int patternCount;
    Expr *cmpExpr;            /* mode=1 */
    Expr *matchExpr;          /* mode=4: regex pattern */
    Expr *guard;              /* optional `|` predicate */
    StringView alias;         /* optional `pattern as name` whole-value binding */
    bool hasAlias;
    Stmt **body;
    int bodyCount;
} CaseBranch;

/* record tag: scope / store / merge */
typedef struct {
    char *key;
    Expr *value;
} RecordTag;



typedef enum {
    EXPR_NUMBER, EXPR_FLOAT, EXPR_STRING, EXPR_IDENT, EXPR_BOOL,
    EXPR_BINARY, EXPR_UNARY, EXPR_CALL, EXPR_MEMBER,
    EXPR_INDEX, EXPR_LIST, EXPR_DICT, EXPR_TAG_ACCESS,
    EXPR_SETLIT, EXPR_SETINTERVAL,
    EXPR_SETCOMP,
    EXPR_ARROW_CAST,
    EXPR_LAMBDA,
    EXPR_CHAIN_COMPARE,
    EXPR_PROPAGATE
} ExprType;

struct Expr {
    ExprType type;
    union {
        int64_t intVal;
        double floatVal;
        StringView stringVal;
        StringView identName;
        bool boolVal;
        struct { Expr *left; InimerseTokenType op; Expr *right; } binary;
        struct { InimerseTokenType op; Expr *operand; } unary;
        struct { Expr *callee; Expr **args; int argCount; } call;
        struct { Expr *object; StringView member; bool safe; } member;
        struct { Expr *object; Expr *index; } index;
        struct { Expr **items; int count; } list;
        struct { Expr **items; int count; } dict;  /* 交替存储 key/value，count 为键值对个数 */
        struct { Expr *obj; StringView tagName; } tagAccess;
        struct { Expr *object; int typeKind; } arrowCast;
    struct { Expr *set; StringView varName; Expr *cond; } setcomp;
        struct { Expr **items; int count; } setlit;
        struct { StringView base; Expr *lo; Expr *hi; int loInc; int hiInc; } setinterval;
        struct { StringView *params; int paramCount; Expr *body; } lambda;
        struct { Expr **operands; InimerseTokenType *ops; int count; } chain;
        struct { Expr *value; } propagate;
    };
};

struct Tag {
    StringView key;
    Expr *value;
};

typedef enum {
    STMT_USING, STMT_WINDOW, STMT_SHOW, STMT_HIDE,
    STMT_NEW, STMT_BLOCK_DEF, STMT_THREAD_DEF,
    STMT_ON, STMT_IF, STMT_WHILE, STMT_FOR,
    STMT_REPEAT, STMT_DO_UNTIL, STMT_WAIT_UNTIL,
    STMT_WAIT, STMT_STOP, STMT_YIELD, STMT_SAY, STMT_CURSOR,
    STMT_INCLUDE, STMT_EXPR, STMT_DELETE, STMT_ASSIGN,
    STMT_BREAK,
    STMT_FUNC, STMT_RETURN,
    STMT_IMPORT, STMT_MAIN, STMT_GLOBAL, STMT_START,
    STMT_THREAD_CTRL, STMT_JOIN, STMT_THREAD_WAIT,
    STMT_LOCK, STMT_SEND, STMT_RECV,
    STMT_GUI,
    STMT_DECLARE, STMT_CASE, STMT_TYPE,
    STMT_RECORD,
    STMT_WITH,
 STMT_CONST,
 STMT_TAG,
    STMT_BE,
    STMT_TRY,
    STMT_THROW,
    STMT_CONTINUE, STMT_LABEL, STMT_GOTO_LABEL, STMT_THREAD_GOTO
} StmtType;

struct Stmt {
    StmtType type;
    union {
        struct { StringView modName; } usingStmt;
        struct { Expr *width; Expr *height; Expr *title; } windowStmt;
        struct { StringView imagePath; Expr *x, *y, *layer; Tag *tags; int tagCount; } showStmt;
        struct { Expr *imageExpr; } hideStmt;
        struct { Expr *prototype; Expr *x, *y, *z; Tag *tags; int tagCount; Stmt **initStmts; int initCount; } newStmt;
        struct { StringView name; StringView *propertyKeys; Expr **propertyValues; int propCount; Tag *tags; int tagCount; } blockDef;
        struct { StringView name; StringView *params; int paramCount; Stmt **body; int bodyCount; int flags; } threadDef;
        struct { StringView eventName; Expr *arg; Stmt **body; int bodyCount; } onStmt;
        struct { Expr *condition; Stmt **thenBody; int thenCount; Stmt **elseBody; int elseCount; } ifStmt;
        struct { Expr *condition; Stmt **body; int bodyCount; } whileStmt;
        struct { StringView var; Expr *rangeStart; Expr *rangeEnd; Expr *rangeStep; Expr *iterExpr; Stmt **body; int bodyCount; } forStmt;
        struct { Expr *count; Stmt **body; int bodyCount; } repeatStmt;
        struct { Stmt **body; int bodyCount; Expr *condition; } doUntilStmt;
        struct { Expr *condition; } waitUntilStmt;
        struct { Expr *duration; } waitStmt;
        struct { bool stopAll; } stopStmt;
        struct { Expr *message; } sayStmt;
        struct { Expr *imagePath; } cursorStmt;
        struct { StringView filename; } includeStmt;
        struct { Expr *expr; } exprStmt;
        struct { Expr *target; } deleteStmt;
        struct { Expr *target; Expr *value; RecordTag *tags; int tagCount; } assignStmt;
        struct { StringView name; StringView *params; int paramCount; Stmt **body; int bodyCount; } funcDef;
        struct { Expr *value; } returnStmt;
        struct { char *label; } breakStmt;
        struct { char *name; Stmt **body; int bodyCount; } labelStmt;
        struct { char *label; } gotoStmt;
        struct { char *thread; char *label; } threadGotoStmt;
        struct { StringView path; StringView ns; } importStmt;
        struct { Stmt **body; int bodyCount; int flags; } mainStmt;
        struct { StringView *names; int nameCount; } globalStmt;
        struct { StringView name; Expr **args; int argCount; } startStmt;
        struct { int op; StringView name; } threadCtrlStmt;
        struct { StringView name; Expr *timeout; } joinStmt;
        struct { StringView name; int mode; Expr *arg; Expr *timeout; } threadWaitStmt;
        struct { StringView name; int isBlock; Stmt **body; int bodyCount; } lockStmt;
        struct { StringView name; Expr *msg; } sendStmt;
        struct { Expr *target; Expr *timeout; } recvStmt;
        /* Scratch 风格通用 GUI 语句: verb + 参数表达式列�?+ 可选块 */
        struct { StringView verb; Expr **args; int argCount; Stmt **body; int bodyCount; } guiStmt;
        struct { char **keys; double *values; int count; } declareStmt;
        struct {
            Expr *subject;
            CaseBranch *branches;
            int branchCount;
            int isTry;
        } caseStmt;
    struct {
        char *name;
        Expr *value;
        RecordTag *tags;
        int tagCount;
        int isDefault;
    } recordStmt;
    struct {
        RecordTag *tags;
        int tagCount;
        Stmt **body;
        int bodyCount;
    } withStmt;
    struct {
        char *name;
        Expr *value;
    } constStmt;
    struct {
        char *name;
        char **items;
        int count;
    } tagStmt;
    struct { StringView name; Expr *set; Expr *init; } beStmt;
    struct { StringView name; Expr *set; } typeStmt;
    struct { Stmt **body; int bodyCount; StringView varName; Stmt **handler; int handlerCount; Stmt **finallyBody; int finallyCount; } tryStmt;
    struct { Expr *expr; } throwStmt;
    };
};

typedef struct {
    Stmt **stmts;
    int count;
} Program;

#endif
