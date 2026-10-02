#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer lex;
    int no_infix_match; /* set while parsing case-branch bodies: "match" is a branch keyword, not an infix op */
    int loop_depth;  /* loop nesting; task/thread defs inside loops are rejected (silently ineffective) */
    int prevLine;    /* line of the most recently consumed token; keeps postfix if/unless on one line */
} Parser;

Program *parse_program(const char *source);

/* 同 parse_program()，但语法错误不终止进程：诊断写 stderr，返回 NULL。
   给宿主进程必须活下来的嵌入方用（xlang 桥接层）。CLI 仍走 parse_program()/
   parse_program_file()，保持 exit(1) 的既有契约。每线程一次只允许一层。 */
Program *parse_program_recoverable(const char *source);

/* 多文件：从文件解析，自动递归展开 import（每个文件只解析一次，支持循环引用与命名空间） */
Program *parse_program_file(const char *path);

#endif
