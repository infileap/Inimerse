#include "lexer.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

static struct {
    const char *word;
    InimerseTokenType type;
} keywords[] = {
    {"using",   TOK_USING},   {"window",  TOK_WINDOW},  {"show",    TOK_SHOW},
    {"hide",    TOK_HIDE},    {"at",      TOK_AT},      {"layer",   TOK_LAYER},
    {"new",     TOK_NEW},     {"block",   TOK_BLOCK},   {"thread",  TOK_THREAD},  {"task",    TOK_TASK},
    {"endless", TOK_ENDLESS}, {"daemon",  TOK_DAEMON},
    {"restart", TOK_RESTART}, {"single",  TOK_SINGLE},
    {"on",      TOK_ON},      {"start",   TOK_START},
    {"if",      TOK_IF},      {"elif",    TOK_ELIF},    {"else",    TOK_ELSE},
    {"while",   TOK_WHILE},   {"for",     TOK_FOR},     {"in",      TOK_IN},     {"unless",  TOK_UNLESS},
    {"repeat",  TOK_REPEAT},
    {"until",   TOK_UNTIL},   {"till",    TOK_UNTIL},
    {"do",      TOK_DO},
    {"break",   TOK_BREAK},
    {"and",     TOK_AND},     {"or",      TOK_OR},    {"not",     TOK_NOT},
    {"wait",    TOK_WAIT},    {"yield",   TOK_YIELD},    {"stop",    TOK_STOP},    {"all",     TOK_ALL},
    {"say",     TOK_SAY},     {"print",   TOK_SAY},    {"cursor",  TOK_CURSOR},
    {"int",     TOK_INT},     {"float",   TOK_FLOAT},   {"str",     TOK_STR},
    {"bool",    TOK_BOOL},    {"array",   TOK_ARRAY},
    {"true",    TOK_TRUE},    {"false",   TOK_FALSE},
    {"include", TOK_INCLUDE}, {"delete",  TOK_DELETE},  {"this",    TOK_THIS},
    {"func",    TOK_FUNC},    {"fn",      TOK_FUNC},    {"return",  TOK_RETURN},
    {"import",  TOK_IMPORT},  {"as",      TOK_AS},      {"main",    TOK_MAIN},
    {"global",  TOK_GLOBAL},
    {"pause",   TOK_PAUSE},   {"resume",  TOK_RESUME},  {"kill",    TOK_KILL},
    {"join",    TOK_JOIN},    {"lock",    TOK_LOCK},    {"unlock",  TOK_UNLOCK},
    {"send",    TOK_SEND},    {"recv",    TOK_RECV},
    {"stage",   TOK_STAGE},   {"background", TOK_BACKGROUND},
    {"sprite",  TOK_SPRITE},  {"goto",    TOK_GOTO},    {"move",    TOK_MOVE},
    {"box",     TOK_BOX},     {"costume", TOK_COSTUME}, {"face",    TOK_FACE},
    {"turn",    TOK_TURN},    {"point_to", TOK_POINT_TO},
    {"velocity", TOK_VELOCITY}, {"gravity", TOK_GRAVITY}, {"bounce", TOK_BOUNCE},
    {"size",    TOK_SIZE},    {"sound",   TOK_SOUND},   {"music",   TOK_MUSIC},
    {"text",    TOK_TEXT},    {"broadcast", TOK_BROADCAST},
    {"clone",   TOK_CLONE}, {"declare", TOK_DECLARE}, {"case", TOK_CASE}, {"record", TOK_RECORD}, {"recorded", TOK_RECORD}, {"with", TOK_WITH}, {"const", TOK_CONST}, {"autosave", TOK_AUTOSAVE}, {"quit_on_escape", TOK_QUIT_ON_ESCAPE}, {"fullscreen", TOK_FULLSCREEN}, {"fixed", TOK_FIXED}, {"ghost", TOK_GHOST}, {"clickable", TOK_CLICKABLE}, {"drag", TOK_DRAG}, {"secret", TOK_SECRET}, {"tag", TOK_TAG},   {"forever", TOK_FOREVER}, {"when",    TOK_WHEN},
    /* `be` 刻意保留为关键字（不是没删干净）：声明构造已移除，但若把这个词降级成普通
       标识符，旧写法会静默变成别的东西 —— `be = 5` 成了给变量 be 赋值，`x be Byte: 42`
       裂成表达式 x、表达式 be、声明 `Byte: 42`，于是**无声改写全局 Byte**。保留它，旧写法
       就在 src/parser/parser.c:1376 报到一行带行号的错。见 docs/SYNTAX.md §6.3。 */
    {"be",      TOK_BE},     {"type",    TOK_TYPE},   {"min",     TOK_MIN},    {"max",     TOK_MAX},
    {"try",     TOK_TRY},
    {"final",   TOK_FINAL}, {"finally", TOK_FINAL},   {"catch",   TOK_CATCH},
    {"match",   TOK_MATCH}, {"throw",   TOK_THROW},
    {"continue", TOK_CONTINUE}, {"to",      TOK_TO},
    {NULL,      TOK_UNKNOWN}
};

static void skip_whitespace(Lexer *lex) {
    while (lex->src[lex->pos] == ' ' || lex->src[lex->pos] == '\t' ||
           lex->src[lex->pos] == '\n' || lex->src[lex->pos] == '\r') {
        if (lex->src[lex->pos] == '\n' || lex->src[lex->pos] == '\r') {
            lex->line++;
            lex->col = 0;
            if (lex->src[lex->pos] == '\r' && lex->src[lex->pos+1] == '\n')
                lex->pos++;
        }
        lex->pos++;
    }
}

static Token make_token(InimerseTokenType type, const char *start, int len) {
    Token tok;
    tok.type = type;
    tok.text.start = start;
    tok.text.length = len;
    tok.intVal = 0;
    tok.floatVal = 0.0;
    return tok;
}

static void skip_comment(Lexer *lex) {
    if (lex->src[lex->pos] == '/' && lex->src[lex->pos+1] == '/') {
        /* A4: // line comment (C/JS style) */
        lex->pos += 2;
        while (lex->src[lex->pos] && lex->src[lex->pos] != '\n' && lex->src[lex->pos] != '\r')
            lex->pos++;
        if (lex->src[lex->pos] == '\r') lex->pos++;
        if (lex->src[lex->pos] == '\n') lex->pos++;
    } else if (lex->src[lex->pos] == '#') {
        if (lex->src[lex->pos+1] == '[') {
            lex->pos += 2;
            int depth = 1;
            while (lex->src[lex->pos] && depth > 0) {
                if (lex->src[lex->pos] == '[') depth++;
                else if (lex->src[lex->pos] == ']') depth--;
                if (depth > 0) lex->pos++;
            }
            if (lex->src[lex->pos] == ']') lex->pos++;
        }
        while (lex->src[lex->pos] && lex->src[lex->pos] != '\n' && lex->src[lex->pos] != '\r')
            lex->pos++;
        if (lex->src[lex->pos] == '\r') lex->pos++;
        if (lex->src[lex->pos] == '\n') lex->pos++;
    }
}

Token lexer_next(Lexer *lex) {
    skip_whitespace(lex);
    while (lex->src[lex->pos] == '#' ||
           (lex->src[lex->pos] == '/' && lex->src[lex->pos+1] == '/')) {
        skip_comment(lex);
        skip_whitespace(lex);
    }

    if (lex->src[lex->pos] == '\0')
        return make_token(TOK_EOF, "", 0);

    char c = lex->src[lex->pos];

    if (c == '"' || c == '\'') {
        char qc = c;
        int start = ++lex->pos;
        /* Hk锟?/&+lI锟? */
        int has_esc = 0;
        {
            int q = lex->pos;
            while (lex->src[q] && lex->src[q] != qc &&
                   lex->src[q] != '\n' && lex->src[q] != '\r') {
                if (lex->src[q] == '\\') { has_esc = 1; q += 2; }
                else q++;
            }
            if (lex->src[q] != qc) {
                fprintf(stderr, "Error: unterminated string literal at line %d\n", lex->line);
                exit(1);
            }
        }
        if (!has_esc) {
            /* 锟絣I:锟斤拷(锟?锟?锟? ) */
            int len = 0;
            while (lex->src[lex->pos] && lex->src[lex->pos] != qc &&
                   lex->src[lex->pos] != '\n' && lex->src[lex->pos] != '\r') {
                lex->pos++;
            }
            len = lex->pos - start;
            Token tok = make_token(TOK_STRING, lex->src + start, len);
            lex->pos++;
            return tok;
        }
        /* 	lI:U 0 strbuf */
        {
            char *eb = malloc(512); int ecap = 512;
            int w = 0;
            while (lex->src[lex->pos] && lex->src[lex->pos] != qc &&
                   lex->src[lex->pos] != '\n' && lex->src[lex->pos] != '\r') {
                char ch = lex->src[lex->pos];
                if (w + 8 >= ecap) { ecap *= 2; eb = realloc(eb, (size_t)ecap); }
                if (ch != '\\') { eb[w++] = ch; lex->pos++; continue; }
                /* lI锟? */
                lex->pos++;
                char e = lex->src[lex->pos];
                switch (e) {
                    case 'n': eb[w++] = '\n'; lex->pos++; break;
                    case 't': eb[w++] = '\t'; lex->pos++; break;
                    case 'r': eb[w++] = '\r'; lex->pos++; break;
                    case '\\': eb[w++] = '\\'; lex->pos++; break;
                    case '"':  eb[w++] = '"';  lex->pos++; break;
                    case '\'': eb[w++] = '\''; lex->pos++; break;
                    case '0':  eb[w++] = '\0'; lex->pos++; break;
                    case 'x': {
                        int v = 0; int n = 0;
                        lex->pos++;
                        while (n < 2 && isxdigit((unsigned char)lex->src[lex->pos])) {
                            char h = lex->src[lex->pos];
                            v = v * 16 + (h >= '0' && h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                            lex->pos++; n++;
                        }
                        eb[w++] = (char)v;
                        break;
                    }
                    default:
                        /* *锟絣I:锟結蜏`锟斤拷W&(|锟? */
                        eb[w++] = '\\';
                        if (e) { eb[w++] = e; lex->pos++; }
                        break;
                }
            }
            if (lex->src[lex->pos] != qc) {
                fprintf(stderr, "Error: unterminated string literal at line %d\n", lex->line);
                exit(1);
            }
            eb[w] = '\0';
            Token tok = make_token(TOK_STRING, eb, w);
            lex->pos++;
            return tok;
        }
    }

    if (c == '0' && (lex->src[lex->pos+1] == 'x' || lex->src[lex->pos+1] == 'X')) {
        int start = lex->pos;
        lex->pos += 2;
        while (isxdigit(lex->src[lex->pos])) lex->pos++;
        Token tok = make_token(TOK_NUMBER, lex->src + start, lex->pos - start);
        return tok;
    }

    if (isdigit(c) || (c == '.' && isdigit(lex->src[lex->pos+1]))) {
        int start = lex->pos;
        bool is_float = false;
        while (isdigit(lex->src[lex->pos])) lex->pos++;
        if (lex->src[lex->pos] == '.' && lex->src[lex->pos+1] != '.') {
            is_float = true;
            lex->pos++;
            while (isdigit(lex->src[lex->pos])) lex->pos++;
        }
        /* hex literal: 0x1F / 0Xab (roll back if not valid) */
        if (lex->src[lex->pos] == 'x' || lex->src[lex->pos] == 'X') {
            int hsave = lex->pos;
            lex->pos++;
            if (isxdigit(lex->src[lex->pos])) {
                while (isxdigit(lex->src[lex->pos])) lex->pos++;
            } else {
                lex->pos = hsave;
            }
        }
        /* scientific notation: 1e6 / 1.5e-3 / 2E4 (float literal; roll back if not a valid exponent) */
        if (lex->src[lex->pos] == 'e' || lex->src[lex->pos] == 'E') {
            int save = lex->pos;
            is_float = true;
            lex->pos++;
            if (lex->src[lex->pos] == '+' || lex->src[lex->pos] == '-') lex->pos++;
            if (isdigit(lex->src[lex->pos])) {
                while (isdigit(lex->src[lex->pos])) lex->pos++;
            } else {
                lex->pos = save;  /* "1.5e" is 1.5 followed by identifier e */
            }
        }
        Token tok = make_token(TOK_NUMBER, lex->src + start, lex->pos - start);
        if (is_float) {
            tok.floatVal = strtod(lex->src + start, NULL);
                    } else {
            tok.intVal = strtoll(lex->src + start, NULL, 10);
                    }
        return tok;
    }

    if (isalpha(c) || c == '_' || (c & 0x80)) {
        int start = lex->pos;
        while (isalnum(lex->src[lex->pos]) || lex->src[lex->pos] == '_' || (lex->src[lex->pos] & 0x80))
            lex->pos++;
        StringView word = { lex->src + start, lex->pos - start };
        for (int i = 0; keywords[i].word; i++) {
            if (sv_eq_cstr(word, keywords[i].word))
                return make_token(keywords[i].type, word.start, word.length);
        }
        /* Z+/Z- builtin set names: merge immediately-adjacent +/- (not "+=") */
        if (word.length == 1 && word.start[0] == 'Z' &&
            (lex->src[lex->pos] == '+' || lex->src[lex->pos] == '-')) {
            if (!(lex->src[lex->pos] == '+' && lex->src[lex->pos + 1] == '=')) {
                lex->pos++;
                return make_token(TOK_IDENT, word.start, word.length + 1);
            }
        }
        return make_token(TOK_IDENT, word.start, word.length);
    }

    switch (c) {
        case '+': if (lex->src[lex->pos+1] == '=') { lex->pos += 2; return make_token(TOK_PLUS_EQ, "+=", 2); }
        if (lex->src[lex->pos+1] == '+') { lex->pos += 2; return make_token(TOK_PLUS_PLUS, "++", 2); }
        lex->pos++; return make_token(TOK_PLUS, "+", 1);
        case '-': if (lex->src[lex->pos+1] == '=') { lex->pos += 2; return make_token(TOK_MINUS_EQ, "-=", 2); }
        if (lex->src[lex->pos+1] == '-') { lex->pos += 2; return make_token(TOK_MINUS_MINUS, "--", 2); }
            if (lex->src[lex->pos+1] == '>') { lex->pos += 2; return make_token(TOK_ARROW, "->", 2); }
            lex->pos++; return make_token(TOK_MINUS, "-", 1);
        case '*': if (lex->src[lex->pos+1] == '=') { lex->pos += 2; return make_token(TOK_STAR_EQ, "*=", 2); } lex->pos++; return make_token(TOK_STAR, "*", 1);
        case '/': if (lex->src[lex->pos+1] == '=') { lex->pos += 2; return make_token(TOK_SLASH_EQ, "/=", 2); } lex->pos++; return make_token(TOK_SLASH, "/", 1);
        case '%': lex->pos++; return make_token(TOK_PERCENT, "%", 1);
        case '=':
            if (lex->src[lex->pos+1] == '=') { lex->pos += 2; return make_token(TOK_EQEQ, "==", 2); }
            lex->pos++; return make_token(TOK_EQ, "=", 1);
        case '!':
            if (lex->src[lex->pos+1] == '=') { lex->pos += 2; return make_token(TOK_NEQ, "!=", 2); }
            lex->pos++; return make_token(TOK_NOT, "!", 1);
        case '<':
            if (lex->src[lex->pos+1] == '=') { lex->pos += 2; return make_token(TOK_LE, "<=", 2); }
            lex->pos++; return make_token(TOK_LT, "<", 1);
        case '>':
            if (lex->src[lex->pos+1] == '>') { lex->pos += 2; return make_token(TOK_COMPOSE, ">>", 2); }
            if (lex->src[lex->pos+1] == '=') { lex->pos += 2; return make_token(TOK_GE, ">=", 2); }
            lex->pos++; return make_token(TOK_GT, ">", 1);
        case '(': lex->pos++; return make_token(TOK_LPAREN, "(", 1);
        case ')': lex->pos++; return make_token(TOK_RPAREN, ")", 1);
        case '[': lex->pos++; return make_token(TOK_LBRACKET, "[", 1);
        case ']': lex->pos++; return make_token(TOK_RBRACKET, "]", 1);
        case '{': lex->pos++; return make_token(TOK_LBRACE, "{", 1);
        case '}': lex->pos++; return make_token(TOK_RBRACE, "}", 1);
        case ':': lex->pos++; return make_token(TOK_COLON, ":", 1);
        case ';': lex->pos++; return make_token(TOK_SEMI, ";", 1);
        case ',': lex->pos++; return make_token(TOK_COMMA, ",", 1);
        case '$':
            if (lex->src[lex->pos+1] == '"') {
                int fstart = lex->pos + 2;
                int fend = fstart;
                while (lex->src[fend] && lex->src[fend] != '"') fend++;
                if (!lex->src[fend]) { fprintf(stderr, "Error: unterminated string literal\n"); exit(1); }
                lex->pos = fend + 1;
                return make_token(TOK_FSTRING, lex->src + fstart, fend - fstart);
            }
            lex->pos++;
            return make_token(TOK_UNKNOWN, "$", 1);
        case '&': if (lex->src[lex->pos+1] == '&') { lex->pos += 2; return make_token(TOK_AND, "&&", 2); } lex->pos++; return make_token(TOK_UNKNOWN, "&", 1);
        case '~': lex->pos++; return make_token(TOK_TILDE, "~", 1);
        case '?': lex->pos++; return make_token(TOK_QUESTION, "?", 1);
        case '|': if (lex->src[lex->pos+1] == '|') { lex->pos += 2; return make_token(TOK_OR, "||", 2); } if (lex->src[lex->pos+1] == '>') { lex->pos += 2; return make_token(TOK_PIPELINE, "|>", 2); } lex->pos++; return make_token(TOK_PIPE, "|", 1);
        case '.':
            if (lex->src[lex->pos+1] == '.') { lex->pos += 2; return make_token(TOK_RANGE, "..", 2); }
            lex->pos++; return make_token(TOK_DOT, ".", 1);
        default:
            lex->pos++;
            return make_token(TOK_UNKNOWN, &lex->src[lex->pos-1], 1);
    }
}

void lexer_init(Lexer *lex, const char *source) {
    lex->src = source;
    lex->pos = 0;
    lex->line = 1;
    lex->col = 0;
    lex->current = lexer_next(lex);
}

Token lexer_peek(Lexer *lex) {
    return lex->current;
}
