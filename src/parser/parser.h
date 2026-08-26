#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer lex;
    int no_infix_match; /* set while parsing case-branch bodies: "match" is a branch keyword, not an infix op */
    int loop_depth;  /* loop nesting; task/thread defs inside loops are rejected (silently ineffective) */
} Parser;

Program *parse_program(const char *source);

/* 多文件：从文件解析，自动递归展开 import（每个文件只解析一次，支持循环引用与命名空间） */
Program *parse_program_file(const char *path);

#endif
