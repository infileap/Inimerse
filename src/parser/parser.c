#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include <ctype.h>

/* AI-era structured errors (--err-json): LLM-friendly {error,line,col,expect,got,fix} */
int g_err_json = 0;

static void err_json_escape(const char *in, char *out, int outsz) {
    int o = 0;
    for (const char *x = in; *x && o < outsz - 2; x++) {
        unsigned char c = (unsigned char)*x;
        if (c == '"') { if (o + 2 < outsz) { out[o++] = '\\'; out[o++] = '"'; } }
        else if (c == '\\') { if (o + 2 < outsz) { out[o++] = '\\'; out[o++] = '\\'; } }
        else if (c == '\n') { if (o + 2 < outsz) { out[o++] = '\\'; out[o++] = 'n'; } }
        else if (c == '\r') { if (o + 2 < outsz) { out[o++] = '\\'; out[o++] = 'r'; } }
        else if (c == '\t') { if (o + 2 < outsz) { out[o++] = '\\'; out[o++] = 't'; } }
        else if (c < 0x20) { if (o + 6 < outsz) { o += snprintf(out + o, outsz - o, "\\u%04x", c); } }
        else if (o < outsz - 1) out[o++] = (char)c;
    }
    out[o] = 0;
}

static void err_json(const char *kind, int line, int col, const char *expect, const char *got, const char *fix) {
    char ex[256], gt[160], fx[512];
    err_json_escape(expect ? expect : "", ex, sizeof ex);
    err_json_escape(got ? got : "", gt, sizeof gt);
    err_json_escape(fix ? fix : "", fx, sizeof fx);
    fprintf(stderr, "{\"error\":\"%s\",\"line\":%d,\"col\":%d,\"expect\":\"%s\",\"got\":\"%s\",\"fix\":\"%s\"}\n",
            kind, line, col, ex, gt, fx);
}

static void advance(Parser *p) { p->lex.current = lexer_next(&p->lex); }
static Token peek(Parser *p) { return p->lex.current; }

static Token peek_next(Parser *p) {
    Lexer saved = p->lex;
    Token tok = lexer_next(&p->lex);
    p->lex = saved;
    return tok;
}

static Token peek_next_next(Parser *p) {
    Lexer saved = p->lex;
    lexer_next(&p->lex);
    Token tok = lexer_next(&p->lex);
    p->lex = saved;
    return tok;
}

static void parse_error_expected(Parser *p, const char *expected, Token got) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*s", (int)got.text.length, got.text.start);
    if (g_err_json) {
        err_json("parse", p->lex.line, p->lex.col, expected, buf, "check the syntax around this token");
        exit(1);
    }
    fprintf(stderr, "Error: expected '%s', but got '%s' (type %d)\n",
            expected, buf, got.type);
    exit(1);
}

static Token consume(Parser *p, InimerseTokenType type, const char *expected) {
    if (peek(p).type != type) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*s", (int)peek(p).text.length, peek(p).text.start);
        if (g_err_json) {
            err_json("parse", p->lex.line, p->lex.col, expected, buf, "insert or fix the expected token at this position");
            exit(1);
        }
        fprintf(stderr, "Error at line %d: expected '%s', but got '%s' (type %d)\n",
                p->lex.line, expected, buf, peek(p).type);
        exit(1);
    }
    Token t = peek(p);
    advance(p);
    return t;
}

static bool match(Parser *p, InimerseTokenType type) {
    if (peek(p).type == type) { advance(p); return true; }
    return false;
}

static Expr *parse_expr(Parser *p);
static Expr *parse_bare_interval(Parser *p);
static bool looks_like_bare_interval(Parser *p);
static Stmt *parse_stmt(Parser *p);
static char *sv_dup(StringView sv) { char *s = malloc(sv.length + 1); memcpy(s, sv.start, sv.length); s[sv.length] = '\0'; return s; }
static Stmt **parse_block(Parser *p, int *count);

/* f-string interpolation: $"a={x} b={y}" -> "a=" + x + " b=" + y (plain identifiers only inside {}) */
static Expr *fstr_append(Expr *acc, Expr *e) {
    if (!acc) return e;
    Expr *bin = malloc(sizeof(Expr));
    bin->type = EXPR_BINARY;
    bin->binary.op = TOK_PLUS;
    bin->binary.left = acc;
    bin->binary.right = e;
    return bin;
}
static Expr *parse_fstring(Parser *p, StringView text) {
    (void)p;
    Expr *acc = NULL;
    const char *s = text.start;
    const char *end = s + text.length;
    char seg[512];
    int segLen = 0;
    while (s < end) {
        if (*s == '{') {
            if (segLen > 0) {
                seg[segLen] = 0;
                Expr *e = malloc(sizeof(Expr));
                e->type = EXPR_STRING;
                e->stringVal.start = strdup(seg); e->stringVal.length = (int)strlen(seg);
                acc = fstr_append(acc, e);
                segLen = 0;
            }
            s++;
            char name[128]; int ni = 0;
            while (s < end && *s != '}' && ni < 120) name[ni++] = *s++;
            name[ni] = 0;
            if (s < end) s++;
            int ok = (name[0] != 0);
            for (int si = 0; si < ni; si++) {
                char ch = name[si];
                if (!(isalnum((unsigned char)ch) || ch == '_')) { ok = 0; break; }
            }
            if (!ok) {
                fprintf(stderr, "Error: f-string interpolation only supports plain identifiers inside {} (got '%s')\n", name);
                exit(1);
            }
            Expr *id = malloc(sizeof(Expr));
            id->type = EXPR_IDENT;
            id->identName.start = strdup(name); id->identName.length = (int)strlen(name);
            acc = fstr_append(acc, id);
        } else {
            if (segLen < 510) seg[segLen++] = *s;
            s++;
        }
    }
    if (segLen > 0) {
        seg[segLen] = 0;
        Expr *e = malloc(sizeof(Expr));
        e->type = EXPR_STRING;
        e->stringVal.start = strdup(seg); e->stringVal.length = (int)strlen(seg);
        acc = fstr_append(acc, e);
    }
    if (!acc) {
        Expr *e = malloc(sizeof(Expr));
        e->type = EXPR_STRING;
        e->stringVal = sv_from_cstr("");
        return e;
    }
    return acc;
}

/* ----- 表达式解�?----- */
static Expr *parse_primary(Parser *p) {
    Token t = peek(p);

    /* 关键字后�?'(' 视为函数调用 */
    if (t.type == TOK_INT || t.type == TOK_FLOAT || t.type == TOK_STR || t.type == TOK_BOOL ||
        t.type == TOK_WINDOW || t.type == TOK_SHOW || t.type == TOK_HIDE ||
        t.type == TOK_NEW || t.type == TOK_DELETE || t.type == TOK_CURSOR ||
        t.type == TOK_JOIN || t.type == TOK_SIZE || t.type == TOK_MATCH) {
        Token next = peek_next(p);
        if (next.type == TOK_LPAREN) {
            const char *fname = "";
            if (t.type == TOK_INT) fname = "int";
            else if (t.type == TOK_FLOAT) fname = "float";
            else if (t.type == TOK_STR) fname = "str";
            else if (t.type == TOK_BOOL) fname = "bool";
            else if (t.type == TOK_WINDOW) fname = "window";
            else if (t.type == TOK_SHOW) fname = "show_image";
            else if (t.type == TOK_HIDE) fname = "hide_image";
            else if (t.type == TOK_NEW) fname = "new_image";
            else if (t.type == TOK_DELETE) fname = "delete_image";
            else if (t.type == TOK_CURSOR) fname = "cursor";
            else if (t.type == TOK_JOIN) fname = "join";
            else if (t.type == TOK_SIZE) fname = "size";
            else if (t.type == TOK_MATCH) fname = "match";
            advance(p);
            Expr *e = malloc(sizeof(Expr));
            e->type = EXPR_IDENT;
            e->identName = sv_from_cstr(fname);
            return e;
        }
    }

    if (t.type == TOK_NUMBER) {
        advance(p);
        int hex = (t.text.length > 2 && t.text.start[0] == '0' && (t.text.start[1] == 'x' || t.text.start[1] == 'X'));
        bool is_float = !hex && (memchr(t.text.start, '.', t.text.length) != NULL ||
                                 memchr(t.text.start, 'e', t.text.length) != NULL ||
                                 memchr(t.text.start, 'E', t.text.length) != NULL);
        if (is_float) {
            double val = strtod(t.text.start, NULL);
            Expr *e = malloc(sizeof(Expr)); e->type = EXPR_FLOAT; e->floatVal = val; return e;
        } else {
            long long val = strtoll(t.text.start, NULL, hex ? 16 : 10);
            Expr *e = malloc(sizeof(Expr)); e->type = EXPR_NUMBER; e->intVal = val; return e;
        }
    }
    if (t.type == TOK_STRING) {
        advance(p);
        Expr *e = malloc(sizeof(Expr)); e->type = EXPR_STRING; e->stringVal = t.text; return e;
    }
    if (t.type == TOK_FSTRING) {
        advance(p);
        return parse_fstring(p, t.text);
    }
    if (t.type == TOK_TRUE || t.type == TOK_FALSE) {
        advance(p);
        Expr *e = malloc(sizeof(Expr)); e->type = EXPR_BOOL; e->boolVal = (t.type == TOK_TRUE); return e;
    }
    if (t.type == TOK_IDENT) {
        advance(p);
        Expr *e = malloc(sizeof(Expr)); e->type = EXPR_IDENT; e->identName = t.text; return e;
    }
    if (t.type == TOK_THIS) {
        advance(p);
        Expr *e = malloc(sizeof(Expr)); e->type = EXPR_IDENT; e->identName = sv_from_cstr("this"); return e;
    }
    if (t.type == TOK_LPAREN || t.type == TOK_LBRACKET) {
        if (looks_like_bare_interval(p)) return parse_bare_interval(p);
    }
    if (t.type == TOK_LPAREN) {
        advance(p);
        Expr *e = parse_expr(p);
        consume(p, TOK_RPAREN, "')'");
        return e;
    }
    if (t.type == TOK_LBRACKET) {
        advance(p);
        Expr *e = malloc(sizeof(Expr)); e->type = EXPR_LIST; e->list.items = NULL; e->list.count = 0;
        if (peek(p).type != TOK_RBRACKET) {
            do {
                Expr *item = parse_expr(p);
                e->list.items = realloc(e->list.items, (e->list.count+1)*sizeof(Expr*));
                e->list.items[e->list.count++] = item;
            if (!match(p, TOK_COMMA) || peek(p).type == TOK_RBRACKET) break;
            } while (1);
        }
        consume(p, TOK_RBRACKET, "']'");
        return e;
    }
    if (t.type == TOK_LBRACE) {
        Token n1 = peek_next(p);
        Token n2 = peek_next_next(p);
        if (n1.type == TOK_IDENT && n2.type == TOK_IN) {
            advance(p); /* { */
            Expr *e = malloc(sizeof(Expr)); e->type = EXPR_SETCOMP;
            Token v = consume(p, TOK_IDENT, "comprehension variable");
            e->setcomp.varName = v.text;
            consume(p, TOK_IN, "'in'");
            e->setcomp.set = parse_expr(p);
            consume(p, TOK_PIPE, "'|'");
            e->setcomp.cond = parse_expr(p);
            consume(p, TOK_RBRACE, "'}'");
            return e;
        }

        /* 字典字面量：{ "key": value, ... }（表达式位置，与语句块区分） */
        advance(p);
        Expr *e = malloc(sizeof(Expr)); e->type = EXPR_DICT; e->dict.items = NULL; e->dict.count = 0;
        if (peek(p).type != TOK_RBRACE) {
            do {
                Expr *key = parse_expr(p);
        consume(p, TOK_COLON, "':'");
                Expr *val = parse_expr(p);
                e->dict.items = realloc(e->dict.items, (e->dict.count*2+2)*sizeof(Expr*));
                e->dict.items[e->dict.count*2] = key;
                e->dict.items[e->dict.count*2+1] = val;
                e->dict.count++;
            if (!match(p, TOK_COMMA) || peek(p).type == TOK_RBRACE) break;
            } while (1);
        }
        consume(p, TOK_RBRACE, "'}'");
        return e;
    }
    parse_error_expected(p, "expression", t);
    return NULL;
}

static Expr *parse_postfix(Parser *p);

/* ----- set support ----- */
static bool name_is_builtin_set(StringView name) {
    if (name.length == 1) {
        char c = name.start[0];
        if (c == 'N' || c == 'Z') return true;
    }
    if (name.length == 2 && name.start[0] == 'Z' && name.start[1] == '+') return true;
    if (name.length == 2 && name.start[0] == 'Z' && name.start[1] == '-') return true;
    if (name.length == 6) { /* Float1..Float9 / float1..float9: 6th char must be a digit */
        char first = name.start[0];
        char d6 = name.start[5];
        if ((first == 'F' || first == 'f') && d6 >= '1' && d6 <= '9' &&
            name.start[1] == 'l' && name.start[2] == 'o' && name.start[3] == 'a' && name.start[4] == 't')
            return true;
    }
    return false;
}

/* scan ahead (no consume) for '~' inside the bracket pair, or builtin-set + '(' form */
static bool looks_like_interval(Parser *p, InimerseTokenType open, StringView name) {
    if (open == TOK_LPAREN && name_is_builtin_set(name)) return true; /* Float2(3,10) */
    Lexer saved = p->lex;
    lexer_next(&p->lex); /* consume open bracket in the scratch lexer */
    Token t2 = lexer_next(&p->lex);
    if (t2.type == TOK_TILDE) { p->lex = saved; return true; }
    if (t2.type != TOK_NUMBER && t2.type != TOK_MINUS) {
        p->lex = saved;
        return false;
    }
    bool has_tilde = false;
    int depth = 0;
    for (;;) {
        Token tk = lexer_next(&p->lex);
        if (tk.type == TOK_TILDE) { has_tilde = true; break; }
        if (tk.type == TOK_LBRACKET || tk.type == TOK_LPAREN) depth++;
        else if (tk.type == TOK_RBRACKET || tk.type == TOK_RPAREN) { if (depth == 0) break; depth--; }
        else if (tk.type == TOK_EOF) break;
    }
    p->lex = saved;
    if (has_tilde) return true;
    return false;
}

static Expr *parse_set_interval(Parser *p, StringView base) {
    Token open = peek(p);
    advance(p);
    int loInc = (open.type == TOK_LBRACKET);
    Expr *lo = NULL, *hi = NULL;
    if (peek(p).type == TOK_COMMA || peek(p).type == TOK_TILDE) {
        advance(p); /* empty lower bound: Z(,5] Z[~5] Z(,) */
    } else if (peek(p).type != TOK_RBRACKET && peek(p).type != TOK_RPAREN) {
        lo = parse_expr(p);
        if (!match(p, TOK_COMMA) && !match(p, TOK_TILDE))
            parse_error_expected(p, "',' or '~'", peek(p));
    }
    if (peek(p).type != TOK_RBRACKET && peek(p).type != TOK_RPAREN)
        hi = parse_expr(p);
    Token close = (peek(p).type == TOK_RBRACKET) ? consume(p, TOK_RBRACKET, "']'") : consume(p, TOK_RPAREN, "')'");
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_SETINTERVAL;
    e->setinterval.base = base;
    e->setinterval.lo = lo;
    e->setinterval.hi = hi;
    e->setinterval.loInc = loInc;
    e->setinterval.hiInc = (close.type == TOK_RBRACKET);
    return e;
}

/* bare interval literal: (,) (,a) (a,) (a,b) [a~b] [~b] [a~] - no builtin prefix (R implied) */
static bool looks_like_bare_interval(Parser *p) {
    Token t1 = peek(p);
    if (t1.type != TOK_LPAREN && t1.type != TOK_LBRACKET) return false;
    Lexer saved = p->lex;
    int depth = 0;
    int sawSep = 0; /* saw a top-level ',' or '~' */
    for (;;) {
        Token tk = lexer_next(&p->lex);
        if (tk.type == TOK_EOF) break;
        if (tk.type == TOK_LPAREN || tk.type == TOK_LBRACKET) { depth++; continue; }
        if (tk.type == TOK_RPAREN || tk.type == TOK_RBRACKET) {
            if (depth == 0) {
                int closedByParen = (tk.type == TOK_RPAREN);
                if (t1.type == TOK_LPAREN) { p->lex = saved; return sawSep; }        /* (a,b] (a,b) (,) */
                if (closedByParen) { p->lex = saved; return sawSep; }                /* [a,) [a,b) */
                p->lex = saved; return 0;                                            /* [a,b] [a~b]? ~ handled below */
            }
            depth--;
            continue;
        }
        if (depth == 0) {
            if (tk.type == TOK_TILDE) { p->lex = saved; return true; }              /* ~ always interval */
            if (tk.type == TOK_COMMA) sawSep = 1;
        }
    }
    p->lex = saved;
    return false;
}

static Expr *parse_bare_interval(Parser *p) {
    Token open = peek(p);
    advance(p);
    int loInc = (open.type == TOK_LBRACKET);
    Expr *lo = NULL, *hi = NULL;
    if (peek(p).type == TOK_COMMA || peek(p).type == TOK_TILDE) {
        advance(p); /* empty lower bound: (,) (,5] [~5] */
    } else {
        lo = parse_expr(p);
        if (!match(p, TOK_COMMA) && !match(p, TOK_TILDE))
            parse_error_expected(p, "',' or '~'", peek(p));
    }
    if (peek(p).type != TOK_RBRACKET && peek(p).type != TOK_RPAREN)
        hi = parse_expr(p);
    Token close = (peek(p).type == TOK_RBRACKET) ? consume(p, TOK_RBRACKET, "']'") : consume(p, TOK_RPAREN, "')'");
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_SETINTERVAL;
    e->setinterval.base = sv_from_cstr("R");
    e->setinterval.lo = lo;
    e->setinterval.hi = hi;
    e->setinterval.loInc = loInc;
    e->setinterval.hiInc = (close.type == TOK_RBRACKET);
    return e;
}

static bool looks_like_set_start(Parser *p) {
    Token t1 = peek(p), t2 = peek_next(p);
    if (t1.type == TOK_NUMBER || t1.type == TOK_STRING) return t2.type == TOK_COMMA;
    if (t1.type == TOK_IDENT) {
        if (t2.type == TOK_COMMA) return true;
        if (t2.type == TOK_LBRACKET || t2.type == TOK_LPAREN)
            return looks_like_interval(p, t2.type, t1.text);
    }
    return false;
}

static Expr *parse_set_literal(Parser *p) {
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_SETLIT;
    e->setlit.items = NULL;
    e->setlit.count = 0;
    do {
        Expr *item = parse_postfix(p);
        e->setlit.items = realloc(e->setlit.items, (e->setlit.count + 1) * sizeof(Expr*));
        e->setlit.items[e->setlit.count++] = item;
    } while (match(p, TOK_COMMA));
    return e;
}

static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    while (1) {
        Token t = peek(p);
        if ((t.type == TOK_LBRACKET || t.type == TOK_LPAREN) && e->type == EXPR_IDENT &&
            looks_like_interval(p, t.type, e->identName)) {
            StringView base = e->identName;
            e = parse_set_interval(p, base);
        }
        else if (t.type == TOK_QUESTION && peek_next(p).type == TOK_DOT) {
            advance(p); advance(p);
            Token name = consume(p, TOK_IDENT, "identifier");
            Expr *mem = calloc(1, sizeof(*mem));
            mem->type = EXPR_MEMBER; mem->member.object = e; mem->member.member = name.text; mem->member.safe = true;
            e = mem;
        }
        else if (t.type == TOK_DOT) {
            advance(p);
            Token name;
            int nt = peek(p).type;
            if (nt == TOK_IDENT) {
                name = consume(p, TOK_IDENT, "identifier");
            } else if (nt == TOK_INT || nt == TOK_FLOAT || nt == TOK_STR || nt == TOK_BOOL || nt == TOK_MATCH) {
                advance(p);
                name.type = nt;
                if (nt == TOK_INT) name.text = sv_from_cstr("int");
                else if (nt == TOK_FLOAT) name.text = sv_from_cstr("float");
                else if (nt == TOK_STR) name.text = sv_from_cstr("str");
                else if (nt == TOK_BOOL) name.text = sv_from_cstr("bool");
                else name.text = sv_from_cstr("match");
            } else {
                name = consume(p, TOK_IDENT, "identifier");
            }
            Expr *mem = malloc(sizeof(Expr));
            mem->type = EXPR_MEMBER;
            mem->member.object = e;
            mem->member.member = name.text;
            e = mem;
        }
        else if (t.type == TOK_LBRACKET) { advance(p); Expr *index = parse_expr(p); consume(p, TOK_RBRACKET, "']'"); Expr *idx = malloc(sizeof(Expr)); idx->type = EXPR_INDEX; idx->index.object = e; idx->index.index = index; e = idx; }
        else if (t.type == TOK_QUESTION && peek_next(p).type == TOK_QUESTION) {
            /* `??` is an infix nil-coalescing operator; leave both tokens
               for parse_expr while a single `?` remains Result propagation. */
            break;
        }
        else if (t.type == TOK_QUESTION) {
            advance(p);
            Expr *prop = calloc(1, sizeof(*prop));
            prop->type = EXPR_PROPAGATE;
            prop->propagate.value = e;
            e = prop;
        }
        else if (t.type == TOK_ARROW) {
            advance(p);
            Token tt = peek(p);
            int kind = -1;
            if (tt.type == TOK_INT) kind = 0;
            else if (tt.type == TOK_FLOAT) kind = 1;
            else if (tt.type == TOK_STR) kind = 2;
            else if (tt.type == TOK_BOOL) kind = 3;
            if (kind < 0) { parse_error_expected(p, "type (int/float/str/bool) after '->'", tt); }
            advance(p);
            Expr *cast = malloc(sizeof(Expr));
            cast->type = EXPR_ARROW_CAST;
            cast->arrowCast.object = e;
            cast->arrowCast.typeKind = kind;
            e = cast;
        }
        else if (t.type == TOK_LPAREN) {
                Token after = peek_next(p); if (after.type == TOK_IDENT) { Token nxt2 = peek_next_next(p); if (nxt2.type == TOK_COLON || nxt2.type == TOK_EQ) break; }
            advance(p); Expr *call = malloc(sizeof(Expr)); call->type = EXPR_CALL; call->call.callee = e; call->call.args = NULL; call->call.argCount = 0;
            if (peek(p).type != TOK_RPAREN) {
                do {
                    Expr *arg = parse_expr(p);
                    call->call.args = realloc(call->call.args, (call->call.argCount+1)*sizeof(Expr*));
                    call->call.args[call->call.argCount++] = arg;
                if (!match(p, TOK_COMMA) || peek(p).type == TOK_RPAREN) break;
                } while (1);
            }
            consume(p, TOK_RPAREN, "')'"); e = call;
        } else break;
    }
    return e;
}

static Expr *parse_unary(Parser *p) {
    if (peek(p).type == TOK_MIN || peek(p).type == TOK_MAX) {
        Token op = peek(p); advance(p); Expr *operand = parse_unary(p);
        Expr *u = malloc(sizeof(Expr)); u->type = EXPR_UNARY; u->unary.op = op.type; u->unary.operand = operand; return u;
    }
    if (peek(p).type == TOK_MINUS || peek(p).type == TOK_PLUS || peek(p).type == TOK_NOT) {
        Token op = peek(p); advance(p); Expr *operand = parse_unary(p);
        Expr *u = malloc(sizeof(Expr)); u->type = EXPR_UNARY; u->unary.op = op.type; u->unary.operand = operand; return u;
    }
    return parse_postfix(p);
}

static Expr *parse_mul_div(Parser *p) {
    Expr *e = parse_unary(p);
    while (peek(p).type == TOK_STAR || peek(p).type == TOK_SLASH || peek(p).type == TOK_PERCENT) {
        Token op = peek(p); advance(p); Expr *right = parse_unary(p);
        Expr *bin = malloc(sizeof(Expr)); bin->type = EXPR_BINARY; bin->binary.left = e; bin->binary.op = op.type; bin->binary.right = right; e = bin;
    }
    return e;
}

static Expr *parse_add_sub(Parser *p) {
    Expr *e = parse_mul_div(p);
    while (peek(p).type == TOK_PLUS || peek(p).type == TOK_MINUS) {
        Token op = peek(p); advance(p); Expr *right = parse_mul_div(p);
        Expr *bin = malloc(sizeof(Expr)); bin->type = EXPR_BINARY; bin->binary.left = e; bin->binary.op = op.type; bin->binary.right = right; e = bin;
    }
    return e;
}

static Expr *parse_comparison(Parser *p) {
    Expr *e = parse_add_sub(p);
    Expr **operands = NULL;
    InimerseTokenType *ops = NULL;
    int count = 0;
    while (peek(p).type == TOK_EQEQ || peek(p).type == TOK_NEQ || peek(p).type == TOK_LT || peek(p).type == TOK_GT ||
           peek(p).type == TOK_IN || peek(p).type == TOK_LE || peek(p).type == TOK_GE ||
           (!p->no_infix_match && peek(p).type == TOK_MATCH)) {
        Token op = peek(p); advance(p); Expr *right = parse_add_sub(p);
        if (op.type == TOK_LT || op.type == TOK_GT || op.type == TOK_LE || op.type == TOK_GE) {
            if (count == 0) {
                operands = malloc(2 * sizeof(*operands)); ops = malloc(sizeof(*ops));
                operands[0] = e; operands[1] = right; ops[0] = op.type; count = 2;
            } else {
                operands = realloc(operands, (size_t)(count + 1) * sizeof(*operands));
                ops = realloc(ops, (size_t)count * sizeof(*ops));
                operands[count++] = right; ops[count - 2] = op.type;
            }
            continue;
        }
        if (op.type == TOK_MATCH) {
            /* a match b -> match(a, b) builtin call */
            Expr *call = malloc(sizeof(Expr));
            call->type = EXPR_CALL;
            call->call.callee = malloc(sizeof(Expr));
            call->call.callee->type = EXPR_IDENT;
            call->call.callee->identName = sv_from_cstr("match");
            call->call.args = malloc(2 * sizeof(Expr*));
            call->call.args[0] = e;
            call->call.args[1] = right;
            call->call.argCount = 2;
            e = call;
        } else {
            Expr *bin = malloc(sizeof(Expr)); bin->type = EXPR_BINARY; bin->binary.left = e; bin->binary.op = op.type; bin->binary.right = right; e = bin;
        }
    }
    if (count > 0) {
        Expr *chain = calloc(1, sizeof(*chain)); chain->type = EXPR_CHAIN_COMPARE;
        chain->chain.operands = operands; chain->chain.ops = ops; chain->chain.count = count; e = chain;
    }
    return e;
}

static Expr *parse_logic_and(Parser *p) {
    Expr *e = parse_comparison(p);
    while (match(p, TOK_AND)) {
        Expr *right = parse_comparison(p);
        Expr *bin = malloc(sizeof(Expr)); bin->type = EXPR_BINARY; bin->binary.left = e; bin->binary.op = TOK_AND; bin->binary.right = right; e = bin;
    }
    return e;
}

static Expr *parse_logic_or(Parser *p) {
    Expr *e = parse_logic_and(p);
    while (match(p, TOK_OR)) {
        Expr *right = parse_logic_and(p);
        Expr *bin = malloc(sizeof(Expr)); bin->type = EXPR_BINARY; bin->binary.left = e; bin->binary.op = TOK_OR; bin->binary.right = right; e = bin;
    }
    return e;
}

static Expr *parse_coalesce(Parser *p) {
    Expr *e = parse_logic_or(p);
    while (peek(p).type == TOK_QUESTION && peek_next(p).type == TOK_QUESTION) {
        advance(p); advance(p);
        Expr *right = parse_logic_or(p);
        Expr *bin = malloc(sizeof(Expr));
        bin->type = EXPR_BINARY; bin->binary.left = e; bin->binary.op = TOK_QUESTION; bin->binary.right = right;
        e = bin;
    }
    return e;
}

/* Core lambda syntax: x -> expr and (a,b) -> expr. */
static Expr *parse_lambda_prefix(Parser *p) {
    Token t = peek(p), n = peek_next(p);
    if (t.type == TOK_IDENT && n.type == TOK_ARROW) {
        advance(p); advance(p); Expr *e = calloc(1, sizeof(*e)); e->type = EXPR_LAMBDA;
        e->lambda.params = malloc(sizeof(StringView)); e->lambda.params[0] = t.text;
        e->lambda.paramCount = 1; e->lambda.body = parse_expr(p); return e;
    }
    if (t.type == TOK_LPAREN && n.type == TOK_IDENT) {
        Token q = peek_next_next(p);
        if (q.type == TOK_ARROW || q.type == TOK_COMMA) {
            advance(p); StringView *ps = NULL; int count = 0;
            do { Token pn = consume(p, TOK_IDENT, "lambda parameter"); ps = realloc(ps, (size_t)(count + 1) * sizeof(*ps)); ps[count++] = pn.text; } while (match(p, TOK_COMMA));
            consume(p, TOK_RPAREN, "')'");
            if (!match(p, TOK_ARROW)) { free(ps); return NULL; }
            Expr *e = calloc(1, sizeof(*e)); e->type = EXPR_LAMBDA; e->lambda.params = ps;
            e->lambda.paramCount = count; e->lambda.body = parse_expr(p); return e;
        }
    }
    return NULL;
}
static Expr *parse_expr(Parser *p) {
    Expr *lambda = parse_lambda_prefix(p);
    if (lambda) return lambda;
    Expr *left = parse_coalesce(p);
    while (match(p, TOK_COMPOSE)) {
        Expr *right = parse_logic_or(p);
        Expr *arg = calloc(1, sizeof(*arg)); arg->type = EXPR_IDENT; arg->identName = sv_from_cstr("__compose_arg");
        Expr *inner = calloc(1, sizeof(*inner)); inner->type = EXPR_CALL; inner->call.callee = left; inner->call.args = malloc(sizeof(Expr *)); inner->call.args[0] = arg; inner->call.argCount = 1;
        Expr *outer = calloc(1, sizeof(*outer)); outer->type = EXPR_CALL; outer->call.callee = right; outer->call.args = malloc(sizeof(Expr *)); outer->call.args[0] = inner; outer->call.argCount = 1;
        Expr *composed = calloc(1, sizeof(*composed)); composed->type = EXPR_LAMBDA; composed->lambda.params = malloc(sizeof(StringView)); composed->lambda.params[0] = sv_from_cstr("__compose_arg"); composed->lambda.paramCount = 1; composed->lambda.body = outer;
        left = composed;
    }
    while (match(p, TOK_PIPELINE)) {
        Expr *right = parse_logic_or(p);
        Expr *call;
        if (right->type == EXPR_CALL) {
            call = right;
            call->call.args = realloc(call->call.args, (size_t)(call->call.argCount + 1) * sizeof(Expr*));
            memmove(call->call.args + 1, call->call.args,
                    (size_t)call->call.argCount * sizeof(Expr*));
            call->call.args[0] = left;
            call->call.argCount++;
        } else {
            call = malloc(sizeof(Expr));
            call->type = EXPR_CALL;
            call->call.callee = right;
            call->call.args = malloc(sizeof(Expr*));
            call->call.args[0] = left;
            call->call.argCount = 1;
        }
        left = call;
    }
    return left;
}

static Tag *parse_with_clause(Parser *p, int *count) {
    *count = 0; Tag *tags = NULL;
    while (peek(p).type == TOK_IDENT) {
        Tag tag; tag.key = consume(p, TOK_IDENT, "label key").text; tag.value = NULL;
        if (match(p, TOK_EQ)) tag.value = parse_expr(p);
        tags = realloc(tags, (*count + 1) * sizeof(Tag)); tags[(*count)++] = tag;
        if (!match(p, TOK_COMMA)) break;
    }
    return tags;
}

static Stmt *parse_simple_stmt(Parser *p);

/* 解析限定名：ident �?ident.ident（返�?StringView，多层时指向 malloc 缓冲�?*/
static StringView parse_qualified_name(Parser *p) {
    Token first = consume(p, TOK_IDENT, "name");
    if (match(p, TOK_DOT)) {
        Token second = consume(p, TOK_IDENT, "name");
        char *buf = malloc(first.text.length + second.text.length + 2);
        memcpy(buf, first.text.start, first.text.length);
        buf[first.text.length] = '.';
        memcpy(buf + first.text.length + 1, second.text.start, second.text.length);
        buf[first.text.length + 1 + second.text.length] = '\0';
        StringView sv; sv.start = buf; sv.length = first.text.length + 1 + second.text.length;
        return sv;
    }
    return first.text;
}

/* 尝试解析线程等待语句：worker.wait 10 / worker.wait until cond[, timeout] /
   u.worker.wait ...（失败时恢复 lexer 并返�?NULL�?*/
static Stmt *try_parse_thread_wait(Parser *p) {
    Lexer saved = p->lex;
    Token a = consume(p, TOK_IDENT, "name");
    StringView name = a.text;
    char *buf = NULL;

    if (peek(p).type == TOK_DOT) {
        Lexer s2 = p->lex;
        advance(p);
        if (peek(p).type == TOK_WAIT) {
            advance(p); /* worker.wait */
        } else if (peek(p).type == TOK_IDENT) {
            Token b = consume(p, TOK_IDENT, "name");
            buf = malloc(a.text.length + b.text.length + 2);
            memcpy(buf, a.text.start, a.text.length);
            buf[a.text.length] = '.';
            memcpy(buf + a.text.length + 1, b.text.start, b.text.length);
            buf[a.text.length + 1 + b.text.length] = '\0';
            name.start = buf; name.length = a.text.length + 1 + b.text.length;
            if (peek(p).type == TOK_DOT) {
                advance(p);
                if (peek(p).type == TOK_WAIT) advance(p);
                else { p->lex = saved; return NULL; }
            } else { p->lex = saved; return NULL; }
        } else { p->lex = saved; return NULL; }
        (void)s2;
    } else if (peek(p).type == TOK_WAIT) {
        advance(p);
    } else {
        p->lex = saved;
        return NULL;
    }

    Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_THREAD_WAIT;
    stmt->threadWaitStmt.name = name;
    if (match(p, TOK_UNTIL)) {
        stmt->threadWaitStmt.mode = 1;
        stmt->threadWaitStmt.arg = parse_expr(p);
        if (match(p, TOK_COMMA)) stmt->threadWaitStmt.timeout = parse_expr(p);
        else stmt->threadWaitStmt.timeout = NULL;
    } else {
        stmt->threadWaitStmt.mode = 0;
        stmt->threadWaitStmt.arg = parse_expr(p);
        stmt->threadWaitStmt.timeout = NULL;
    }
    return stmt;
}

static Stmt **parse_block(Parser *p, int *count) {
    consume(p, TOK_LBRACE, "'{'");
    Stmt **stmts = NULL; *count = 0;
    while (!match(p, TOK_RBRACE) && peek(p).type != TOK_EOF) {
        while (peek(p).type == TOK_SEMI) advance(p);
        if (peek(p).type == TOK_RBRACE || peek(p).type == TOK_EOF) continue;
        Stmt *s = parse_stmt(p);
        stmts = realloc(stmts, (*count + 1) * sizeof(Stmt*));
        stmts[(*count)++] = s;
    }
    return stmts;
}

/* statement body: block or single statement (no braces) - simplified syntax */
static Stmt **parse_body(Parser *p, int *count) {
    if (peek(p).type == TOK_LBRACE)
        return parse_block(p, count);
    Stmt *s = parse_stmt(p);
    Stmt **arr = malloc(sizeof(Stmt*));
    arr[0] = s;
    *count = 1;
    return arr;
}

/* if 语句尾部：elif / else 链（elif 等价�?else + 嵌套 if，支持任意长度链�?*/
static void parse_if_tail(Parser *p, Stmt *stmt) {
    if (match(p, TOK_ELIF)) {
        Stmt *nested = malloc(sizeof(Stmt));
        nested->type = STMT_IF;
        nested->ifStmt.condition = parse_expr(p);
        nested->ifStmt.thenBody = parse_body(p, &nested->ifStmt.thenCount);
        parse_if_tail(p, nested);
        stmt->ifStmt.elseBody = malloc(sizeof(Stmt*));
        stmt->ifStmt.elseBody[0] = nested;
        stmt->ifStmt.elseCount = 1;
    } else if (match(p, TOK_ELSE)) {
        stmt->ifStmt.elseBody = parse_body(p, &stmt->ifStmt.elseCount);
    } else {
        stmt->ifStmt.elseBody = NULL;
        stmt->ifStmt.elseCount = 0;
    }
}

/* Scratch 风格通用 GUI 语句: verb + 逗号分隔参数(精灵名用标识符表达式) + 可选块 */
static int gui_no_args(InimerseTokenType t) {
    switch (t) {
    case TOK_STAGE: case TOK_BACKGROUND: case TOK_SPRITE: case TOK_GOTO: case TOK_MOVE:
    case TOK_QUIT_ON_ESCAPE: case TOK_FULLSCREEN:
    case TOK_BOX: case TOK_COSTUME: case TOK_FACE: case TOK_TURN: case TOK_POINT_TO:
    case TOK_VELOCITY: case TOK_GRAVITY: case TOK_BOUNCE: case TOK_SIZE:
    case TOK_SOUND: case TOK_MUSIC: case TOK_TEXT: case TOK_BROADCAST:
    case TOK_CLONE: case TOK_FOREVER: case TOK_WHEN: case TOK_ON:
    case TOK_STOP: case TOK_WAIT: case TOK_SAY:
    case TOK_IF: case TOK_WHILE: case TOK_FOR: case TOK_REPEAT: case TOK_BREAK:
    case TOK_RETURN: case TOK_FUNC: case TOK_THREAD: case TOK_RBRACE: case TOK_EOF:
    case TOK_LBRACE:
        return 1;
    default:
        return 0;
    }
}
static Stmt *parse_gui_stmt(Parser *p) {
    Token t = peek(p);
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_GUI;
    stmt->guiStmt.verb = t.text;
    stmt->guiStmt.args = NULL; stmt->guiStmt.argCount = 0;
    stmt->guiStmt.body = NULL; stmt->guiStmt.bodyCount = 0;
    advance(p);
    if (!gui_no_args(peek(p).type)) {
        stmt->guiStmt.args = malloc(sizeof(Expr*));
        stmt->guiStmt.args[0] = parse_expr(p);
        stmt->guiStmt.argCount = 1;
        while (match(p, TOK_COMMA)) {
            stmt->guiStmt.args = realloc(stmt->guiStmt.args, (stmt->guiStmt.argCount + 1) * sizeof(Expr*));
            stmt->guiStmt.args[stmt->guiStmt.argCount] = parse_expr(p);
            stmt->guiStmt.argCount++;
        }
    }
    /* text "..." at x, y: 追加坐标参数 */
    if (t.type == TOK_TEXT && match(p, TOK_AT)) {
        stmt->guiStmt.args = realloc(stmt->guiStmt.args, (stmt->guiStmt.argCount + 2) * sizeof(Expr*));
        stmt->guiStmt.args[stmt->guiStmt.argCount++] = parse_expr(p);
        consume(p, TOK_COMMA, "','");
        stmt->guiStmt.args[stmt->guiStmt.argCount++] = parse_expr(p);
    }
    if (peek(p).type == TOK_LBRACE) {
        stmt->guiStmt.body = parse_block(p, &stmt->guiStmt.bodyCount);
    }
    return stmt;
}

/* declare { mem64MB; threads8; time10s; inst1G } resource declaration */
static Stmt *parse_declare(Parser *p) {
    advance(p); /* TOK_DECLARE */
    consume(p, TOK_LBRACE, "'{'");
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_DECLARE;
    stmt->declareStmt.keys = NULL;
    stmt->declareStmt.values = NULL;
    stmt->declareStmt.count = 0;
    while (peek(p).type != TOK_RBRACE && peek(p).type != TOK_EOF) {
        if (peek(p).type == TOK_SEMI || peek(p).type == TOK_COMMA) { advance(p); continue; }
        Token k = consume(p, TOK_IDENT, "limit name (mem/threads/time/inst)");
        Token n = consume(p, TOK_NUMBER, "number");
        char numbuf[64];
        int nlen = n.text.length < 63 ? n.text.length : 63;
        memcpy(numbuf, n.text.start, nlen);
        numbuf[nlen] = '\0';
        int nhex = (nlen > 2 && numbuf[0] == '0' && (numbuf[1] == 'x' || numbuf[1] == 'X'));
        double v = (strchr(numbuf, '.') || strchr(numbuf, 'e') || strchr(numbuf, 'E'))
                   ? strtod(numbuf, NULL) : (double)strtoll(numbuf, NULL, nhex ? 16 : 10);
        char key[64];
        int klen = k.text.length < 63 ? k.text.length : 63;
        memcpy(key, k.text.start, klen);
        key[klen] = '\0';
        /* optional unit suffix: mem B/KB/MB/GB, time ms/s/m, inst K/M/G */
        if (peek(p).type == TOK_IDENT) {
            Token u = peek(p);
            char ustr[16];
            int ulen = u.text.length < 15 ? u.text.length : 15;
            memcpy(ustr, u.text.start, ulen);
            ustr[ulen] = '\0';
            if (strcmp(key, "mem") == 0) {
                if (_stricmp(ustr, "KB") == 0) v *= 1024.0;
                else if (_stricmp(ustr, "MB") == 0) v *= 1024.0 * 1024.0;
                else if (_stricmp(ustr, "GB") == 0) v *= 1024.0 * 1024.0 * 1024.0;
                else if (_stricmp(ustr, "B") != 0) fprintf(stderr, "warning: unknown mem unit '%s' (B/KB/MB/GB)\n", ustr);
                advance(p);
            } else if (strcmp(key, "time") == 0) {
                if (_stricmp(ustr, "ms") == 0) v /= 1000.0;
                else if (_stricmp(ustr, "m") == 0) v *= 60.0;
                else if (_stricmp(ustr, "s") != 0) fprintf(stderr, "warning: unknown time unit '%s' (ms/s/m)\n", ustr);
                advance(p);
            } else if (strcmp(key, "vram") == 0 || strcmp(key, "memv") == 0) {
                if (_stricmp(ustr, "KB") == 0) v *= 1024.0;
                else if (_stricmp(ustr, "MB") == 0) v *= 1024.0 * 1024.0;
                else if (_stricmp(ustr, "GB") == 0) v *= 1024.0 * 1024.0 * 1024.0;
                else if (_stricmp(ustr, "B") != 0) fprintf(stderr, "warning: unknown vram unit '%s' (B/KB/MB/GB)\n", ustr);
                advance(p);
            } else if (strcmp(key, "inst") == 0) {
                if (_stricmp(ustr, "K") == 0) v *= 1000.0;
                else if (_stricmp(ustr, "M") == 0) v *= 1000000.0;
                else if (_stricmp(ustr, "G") == 0) v *= 1000000000.0;
                else fprintf(stderr, "warning: unknown inst unit '%s' (K/M/G)\n", ustr);
                advance(p);
            }
        }
        const char *norm = NULL;
        if (strcmp(key, "mem") == 0) norm = "mem";
        else if (strcmp(key, "threads") == 0) norm = "threads";
        else if (strcmp(key, "time") == 0) norm = "time";
        else if (strcmp(key, "inst") == 0) norm = "inst";
        else if (strcmp(key, "vram") == 0) norm = "vram";
        else {
            fprintf(stderr, "warning: unknown declare item '%s' (mem/threads/time/inst), ignored\n", key);
            continue;
        }
        stmt->declareStmt.keys = realloc(stmt->declareStmt.keys, (stmt->declareStmt.count + 1) * sizeof(char*));
        stmt->declareStmt.values = realloc(stmt->declareStmt.values, (stmt->declareStmt.count + 1) * sizeof(double));
        stmt->declareStmt.keys[stmt->declareStmt.count] = strdup(norm);
        stmt->declareStmt.values[stmt->declareStmt.count] = v;
        stmt->declareStmt.count++;
    }
    consume(p, TOK_RBRACE, "'}'");
    return stmt;
}

/* case expr { pattern... : body ... } */
static Stmt *parse_case(Parser *p) {
    advance(p); /* TOK_CASE */
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_CASE;
    stmt->caseStmt.isTry = match(p, TOK_TRY) ? 1 : 0;
    stmt->caseStmt.subject = parse_expr(p);
    stmt->caseStmt.branches = NULL;
    stmt->caseStmt.branchCount = 0;
    consume(p, TOK_LBRACE, "'{'");
    while (peek(p).type != TOK_RBRACE && peek(p).type != TOK_EOF) {
        if (peek(p).type == TOK_SEMI) { advance(p); continue; }
        stmt->caseStmt.branches = realloc(stmt->caseStmt.branches,
            (stmt->caseStmt.branchCount + 1) * sizeof(CaseBranch));
                CaseBranch *br = &stmt->caseStmt.branches[stmt->caseStmt.branchCount];
        memset(br, 0, sizeof(*br));
        br->mode = 0;
        if (peek(p).type == TOK_ELSE) {
            advance(p);
            br->mode = 2;
        } else if (peek(p).type == TOK_LT || peek(p).type == TOK_GT ||
                   peek(p).type == TOK_LE || peek(p).type == TOK_GE ||
                   peek(p).type == TOK_EQEQ || peek(p).type == TOK_NEQ) {
            br->mode = 1;
            br->cmpOp = peek(p).type; advance(p);
            br->cmpExpr = parse_expr(p);
        } else if (peek(p).type == TOK_IN) {
            advance(p);
            br->mode = 3; /* membership / subset */
            br->cmpExpr = parse_expr(p);
        } else if (peek(p).type == TOK_MATCH) {
            advance(p);
            br->mode = 4; /* regex match */
            br->matchExpr = parse_expr(p);
        } else {
            br->mode = 0;
            for (;;) {
                br->patterns = realloc(br->patterns, (br->patternCount + 1) * sizeof(Expr*));
                br->patterns[br->patternCount++] = parse_expr(p);
                if (peek(p).type == TOK_COMMA) { advance(p); continue; }
                break;
            }
        }
        br->guard = NULL;
        if (match(p, TOK_PIPE)) br->guard = parse_expr(p);
        consume(p, TOK_COLON, "':'");
        if (peek(p).type == TOK_LBRACE) {
            br->body = parse_block(p, &br->bodyCount);
        } else {
            br->body = malloc(sizeof(Stmt*));
            p->no_infix_match = 1;
            br->body[0] = parse_stmt(p);
            p->no_infix_match = 0;
            br->bodyCount = 1;
        }
        if (peek(p).type == TOK_COLON) {
            if (g_err_json) {
                err_json("parse", p->lex.line, p->lex.col, "',' or '}'", ":", "wrap the case action in { } when followed by a comparison branch");
                exit(1);
            }
            fprintf(stderr, "Error: case action expression runs into ':' - wrap the action in { } when followed by a comparison branch.\n");
            exit(1);
        }
        stmt->caseStmt.branchCount++;
    }
    consume(p, TOK_RBRACE, "'}'");
    return stmt;
}

/* ---- record system (parser) ---- */
/* parse tags: (key: value, key: value) when paren=1, else key: value list */
static int parse_record_tags(Parser *p, RecordTag **tags, int paren) {
    int count =0;
    if (paren) consume(p, TOK_LPAREN, "'('");
    while (1) {
        if (paren && peek(p).type == TOK_RPAREN) break;
        if (!paren && (peek(p).type == TOK_LBRACE || peek(p).type == TOK_EOF)) break;
        Token k = consume(p, TOK_IDENT, "tag name (scope/store/merge)");
        if (peek(p).type == TOK_COLON || peek(p).type == TOK_EQ) advance(p);
        Expr *v = parse_expr(p);
        *tags = realloc(*tags, (count +1) * sizeof(RecordTag));
        char keybuf[64];
        int klen = k.text.length <63 ? k.text.length :63;
        memcpy(keybuf, k.text.start, klen);
        keybuf[klen] = '\0';
        (*tags)[count].key = strdup(keybuf);
        (*tags)[count].value = v;
        count++;
        if (peek(p).type == TOK_COMMA) { advance(p); continue; }
        break;
    }
    if (paren) consume(p, TOK_RPAREN, "')'");
    return count;
}

/* record x = v [, tags]  /  record default store = "both"  /  recorded x = v */
/* tag <label> <item1>, <item2>, ...: group sprites under a label (collectibles) */
static Stmt *parse_tag_stmt(Parser *p) {
    advance(p); /* TOK_TAG */
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_TAG;
    Token name = consume(p, TOK_IDENT, "tag name");
    stmt->tagStmt.name = malloc(name.text.length + 1);
    memcpy(stmt->tagStmt.name, name.text.start, name.text.length);
    stmt->tagStmt.name[name.text.length] = '\0';
    stmt->tagStmt.items = NULL;
    stmt->tagStmt.count = 0;
    /* first item: IDENT directly after tag name */
    Token it = consume(p, TOK_IDENT, "tag item");
    stmt->tagStmt.items = malloc(sizeof(char*));
    stmt->tagStmt.items[0] = malloc(it.text.length + 1);
    memcpy(stmt->tagStmt.items[0], it.text.start, it.text.length);
    stmt->tagStmt.items[0][it.text.length] = '\0';
    stmt->tagStmt.count = 1;
    /* more items separated by commas: , item */
    while (match(p, TOK_COMMA)) {
        Token t2 = consume(p, TOK_IDENT, "tag item");
        stmt->tagStmt.items = realloc(stmt->tagStmt.items, (stmt->tagStmt.count + 1) * sizeof(char*));
        stmt->tagStmt.items[stmt->tagStmt.count] = malloc(t2.text.length + 1);
        memcpy(stmt->tagStmt.items[stmt->tagStmt.count], t2.text.start, t2.text.length);
        stmt->tagStmt.items[stmt->tagStmt.count][t2.text.length] = '\0';
        stmt->tagStmt.count++;
    }
    return stmt;
}

/* const x = v: read-only global (compile-time check on assignment) */
static Stmt *parse_const_stmt(Parser *p) {
    advance(p); /* TOK_CONST */
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_CONST;
    Token name = consume(p, TOK_IDENT, "constant name");
    stmt->constStmt.name = malloc(name.text.length + 1);
    memcpy(stmt->constStmt.name, name.text.start, name.text.length);
    stmt->constStmt.name[name.text.length] = '\0';
    consume(p, TOK_EQ, "'='");
    stmt->constStmt.value = parse_expr(p);
    return stmt;
}

static Stmt *parse_record_stmt(Parser *p) {
    advance(p); /* TOK_RECORD */
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_RECORD;
    stmt->recordStmt.name = NULL;
    stmt->recordStmt.value = NULL;
    stmt->recordStmt.tags = NULL;
    stmt->recordStmt.tagCount =0;
    stmt->recordStmt.isDefault =0;
    if (peek(p).type == TOK_IDENT) {
        Token k = peek(p);
        char kb[32];
        int kl = k.text.length <31 ? k.text.length :31;
        memcpy(kb, k.text.start, kl);
        kb[kl] = '\0';
        if (strcmp(kb, "default") ==0) {
            advance(p);
            consume(p, TOK_IDENT, "'store'");
            consume(p, TOK_EQ, "'='");
            stmt->recordStmt.isDefault =1;
            stmt->recordStmt.value = parse_expr(p);
            return stmt;
        }
    }
    Token name = consume(p, TOK_IDENT, "variable name");
    consume(p, TOK_EQ, "'='");
    char *nm = malloc(name.text.length +1);
    memcpy(nm, name.text.start, name.text.length);
    nm[name.text.length] = '\0';
    stmt->recordStmt.name = nm;
    stmt->recordStmt.value = parse_expr(p);
    if (peek(p).type == TOK_COMMA) {
        advance(p);
        stmt->recordStmt.tagCount = parse_record_tags(p, &stmt->recordStmt.tags, 0);
    }
    return stmt;
}

static Stmt *parse_type_stmt(Parser *p) {
    advance(p); /* type */
    Stmt *s = (Stmt *)calloc(1, sizeof(*s));
    s->type = STMT_TYPE;
    Token name = consume(p, TOK_IDENT, "type name");
    s->typeStmt.name = name.text;
    consume(p, TOK_EQ, "'='");
    s->typeStmt.set = looks_like_set_start(p) ? parse_set_literal(p) : parse_expr(p);
    return s;
}

/* with scope: "entity", store: "both" { stmts } */
static Stmt *parse_with_tags_stmt(Parser *p) {
    advance(p); /* TOK_WITH */
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_WITH;
    stmt->withStmt.tags = NULL;
    stmt->withStmt.tagCount = parse_record_tags(p, &stmt->withStmt.tags, 0);
    stmt->withStmt.body = parse_block(p, &stmt->withStmt.bodyCount);
    return stmt;
}

static Stmt *parse_try(Parser *p) {
    advance(p); /* try */
    Stmt *s = malloc(sizeof(Stmt)); s->type = STMT_TRY;
    s->tryStmt.body = NULL; s->tryStmt.bodyCount = 0;
    s->tryStmt.handler = NULL; s->tryStmt.handlerCount = 0;
    s->tryStmt.varName.start = NULL; s->tryStmt.varName.length = 0;
    s->tryStmt.finallyBody = NULL; s->tryStmt.finallyCount = 0;
    consume(p, TOK_LBRACE, "'{'");
    while (peek(p).type != TOK_RBRACE && peek(p).type != TOK_EOF) {
        s->tryStmt.body = realloc(s->tryStmt.body, (s->tryStmt.bodyCount + 1) * sizeof(Stmt*));
        s->tryStmt.body[s->tryStmt.bodyCount++] = parse_stmt(p);
    }
    consume(p, TOK_RBRACE, "'}'");
    if (match(p, TOK_CATCH)) {
        if (match(p, TOK_LPAREN)) {
            Token v = consume(p, TOK_IDENT, "catch variable");
            s->tryStmt.varName = v.text;
            consume(p, TOK_RPAREN, "')'");
        }
        consume(p, TOK_LBRACE, "'{'");
        while (peek(p).type != TOK_RBRACE && peek(p).type != TOK_EOF) {
            s->tryStmt.handler = realloc(s->tryStmt.handler, (s->tryStmt.handlerCount + 1) * sizeof(Stmt*));
            s->tryStmt.handler[s->tryStmt.handlerCount++] = parse_stmt(p);
        }
        consume(p, TOK_RBRACE, "'}'");
    }
    if (match(p, TOK_FINAL)) {
        consume(p, TOK_LBRACE, "'{'");
        while (peek(p).type != TOK_RBRACE && peek(p).type != TOK_EOF) {
            s->tryStmt.finallyBody = realloc(s->tryStmt.finallyBody, (s->tryStmt.finallyCount + 1) * sizeof(Stmt*));
            s->tryStmt.finallyBody[s->tryStmt.finallyCount++] = parse_stmt(p);
        }
        consume(p, TOK_RBRACE, "'}'");
    }
    return s;
}

static Stmt *parse_throw(Parser *p) {
    advance(p); /* throw */
    Stmt *s = malloc(sizeof(Stmt)); s->type = STMT_THROW;
    s->throwStmt.expr = parse_expr(p);
    return s;
}

static Stmt *parse_stmt(Parser *p) {
    Token t = peek(p);

    /* resource declaration: declare { ... } */
    if (t.type == TOK_DECLARE) return parse_declare(p);
    if (t.type == TOK_CASE) return parse_case(p);
    if (t.type == TOK_TYPE) return parse_type_stmt(p);
    if (t.type == TOK_RECORD) return parse_record_stmt(p);
    if (t.type == TOK_CONST || t.type == TOK_FINAL) return parse_const_stmt(p);
    if (t.type == TOK_TAG) return parse_tag_stmt(p);
    if (t.type == TOK_WITH) return parse_with_tags_stmt(p);

    /* Scratch 风格 GUI 语句(show/hide 无字符串参数时也走这�? */
    if (t.type == TOK_STAGE || t.type == TOK_BACKGROUND || t.type == TOK_SPRITE ||
        t.type == TOK_GOTO || t.type == TOK_MOVE || t.type == TOK_BOX ||
        t.type == TOK_COSTUME || t.type == TOK_FACE || t.type == TOK_TURN ||
        t.type == TOK_POINT_TO || t.type == TOK_VELOCITY || t.type == TOK_GRAVITY ||
        t.type == TOK_BOUNCE || t.type == TOK_SIZE || t.type == TOK_SOUND ||
        t.type == TOK_BOUNCE || t.type == TOK_SIZE || t.type == TOK_SOUND ||
        t.type == TOK_MUSIC || t.type == TOK_TEXT || t.type == TOK_AUTOSAVE ||
        t.type == TOK_QUIT_ON_ESCAPE || t.type == TOK_FULLSCREEN ||
        t.type == TOK_FIXED || t.type == TOK_GHOST || t.type == TOK_CLICKABLE ||
        t.type == TOK_DRAG || t.type == TOK_SECRET || t.type == TOK_BROADCAST ||
        t.type == TOK_CLONE || t.type == TOK_FOREVER || t.type == TOK_WHEN ||
        t.type == TOK_ON ||
        ((t.type == TOK_SHOW || t.type == TOK_HIDE) && peek_next(p).type != TOK_STRING)) {
        return parse_gui_stmt(p);
    }

    /* 图形关键字后�?'(' 时按函数调用处理 */
    if (t.type == TOK_WINDOW || t.type == TOK_SHOW || t.type == TOK_HIDE ||
        t.type == TOK_NEW || t.type == TOK_DELETE || t.type == TOK_CURSOR) {
        Token next = peek_next(p);
        if (next.type == TOK_LPAREN) {
            Expr *expr = parse_expr(p);
            Stmt *stmt = malloc(sizeof(Stmt));
            stmt->type = STMT_EXPR;
            stmt->exprStmt.expr = expr;
            return stmt;
        }
    }

    if (t.type == TOK_RESTART) {
        advance(p);
        Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_THREAD_CTRL; stmt->threadCtrlStmt.op = THREAD_OP_RESTART;
        if (peek(p).type == TOK_THIS) { advance(p); stmt->threadCtrlStmt.name = sv_from_cstr("this"); }
        else stmt->threadCtrlStmt.name = consume(p, TOK_IDENT, "thread name").text;
        return stmt;
    }
    if (t.type == TOK_YIELD) {
        advance(p); Stmt *ys = malloc(sizeof(Stmt)); ys->type = STMT_YIELD; return ys;
    }
    if (t.type == TOK_WAIT) {
        advance(p);
        if (match(p, TOK_UNTIL)) { Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_WAIT_UNTIL; stmt->waitUntilStmt.condition = parse_expr(p); return stmt; }
        else { Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_WAIT; stmt->waitStmt.duration = parse_expr(p); return stmt; }
    }

    /* 线程操作：worker.wait 10 / u.worker.wait until cond[, timeout] */
    if (t.type == TOK_IDENT) {
        Stmt *tw = try_parse_thread_wait(p);
        if (tw) return tw;
        {
            Token nxt = peek_next(p);
            if (nxt.type == TOK_COLON) {
                /* label block: A: { ... } / A: stmt / A: while ... {} */
                Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_LABEL;
                stmt->labelStmt.name = sv_dup(t.text);
                advance(p); advance(p); /* ident, ':' */
                if (peek(p).type == TOK_LBRACE) {
                    stmt->labelStmt.body = parse_block(p, &stmt->labelStmt.bodyCount);
                } else {
                    stmt->labelStmt.body = malloc(sizeof(Stmt*));
                    stmt->labelStmt.body[0] = parse_stmt(p);
                    stmt->labelStmt.bodyCount = 1;
                }
                return stmt;
            }
            if (nxt.type == TOK_TO) {
                /* thread jump: thread1 to A */
                Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_THREAD_GOTO;
                stmt->threadGotoStmt.thread = sv_dup(t.text);
                advance(p); advance(p); /* ident, TOK_TO */
                Token lt = consume(p, TOK_IDENT, "label after 'thread to'");
                stmt->threadGotoStmt.label = sv_dup(lt.text);
                return stmt;
            }
        }
        if (peek_next(p).type == TOK_BE) {
            Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_BE;
            stmt->beStmt.name = consume(p, TOK_IDENT, "name").text;
            advance(p); /* TOK_BE */
            stmt->beStmt.set = looks_like_set_start(p) ? parse_set_literal(p) : parse_expr(p);
            stmt->beStmt.init = NULL;
            if (match(p, TOK_COLON)) stmt->beStmt.init = parse_expr(p);
            return stmt;
        }
    }

    if (t.type == TOK_USING || t.type == TOK_BLOCK || t.type == TOK_INCLUDE ||
        t.type == TOK_SAY || t.type == TOK_STOP ||
        t.type == TOK_INT || t.type == TOK_FLOAT || t.type == TOK_STR ||
        t.type == TOK_BOOL || t.type == TOK_ARRAY)
        return parse_simple_stmt(p);

    if (t.type == TOK_TRY) return parse_try(p);
    if (t.type == TOK_THROW) return parse_throw(p);

    /* label jump: to A / continue */
    if (t.type == TOK_TO) {
        advance(p);
        Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_GOTO_LABEL;
        Token lt = consume(p, TOK_IDENT, "label after 'to'");
        stmt->gotoStmt.label = sv_dup(lt.text);
        return stmt;
    }
    if (t.type == TOK_CONTINUE) {
        advance(p);
        Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_CONTINUE;
        return stmt;
    }

    if (t.type == TOK_IF) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_IF;
        stmt->ifStmt.condition = parse_expr(p);
        stmt->ifStmt.thenBody = parse_body(p, &stmt->ifStmt.thenCount);
        parse_if_tail(p, stmt);
        return stmt;
    }
    else if (t.type == TOK_WHILE) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_WHILE;
        stmt->whileStmt.condition = parse_expr(p);
        p->loop_depth++;
        stmt->whileStmt.body = parse_body(p, &stmt->whileStmt.bodyCount);
        p->loop_depth--;
        return stmt;
    }
    else if (t.type == TOK_FOR) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_FOR;
        stmt->forStmt.var = consume(p, TOK_IDENT, "variable").text; consume(p, TOK_IN, "'in'");
        if (peek(p).type == TOK_IDENT && sv_eq_cstr(peek(p).text, "range")) {
            advance(p); consume(p, TOK_LPAREN, "'('");
            if (peek(p).type == TOK_RPAREN) { fprintf(stderr, "range requires arguments\n"); exit(1); }
            Expr *arg1 = parse_expr(p);
            if (match(p, TOK_COMMA)) { stmt->forStmt.rangeStart = arg1; stmt->forStmt.rangeEnd = parse_expr(p); if (match(p, TOK_COMMA)) stmt->forStmt.rangeStep = parse_expr(p); else stmt->forStmt.rangeStep = NULL; }
            else { stmt->forStmt.rangeStart = NULL; stmt->forStmt.rangeEnd = arg1; stmt->forStmt.rangeStep = NULL; }
            consume(p, TOK_RPAREN, "')'");
            stmt->forStmt.iterExpr = NULL;
        } else {
            /* for x in <数组表达�? */
            Token t1 = peek(p);
            Token t2 = peek_next(p);
            bool is_range_sugar = (t2.type == TOK_RANGE);
            if (!is_range_sugar && t1.type == TOK_MINUS && peek_next_next(p).type == TOK_RANGE)
                is_range_sugar = true;
            if (is_range_sugar) {
                Expr *a = parse_expr(p);
                consume(p, TOK_RANGE, "'..'");
                Expr *b = parse_expr(p);
                Expr *c = NULL;
                if (match(p, TOK_RANGE)) c = parse_expr(p);
                stmt->forStmt.rangeStart = a;
                stmt->forStmt.rangeEnd = b;
                stmt->forStmt.rangeStep = c;
                stmt->forStmt.iterExpr = NULL;
            } else {
                stmt->forStmt.iterExpr = parse_expr(p);
                stmt->forStmt.rangeStart = stmt->forStmt.rangeEnd = stmt->forStmt.rangeStep = NULL;
            }
        }
        p->loop_depth++;
        stmt->forStmt.body = parse_body(p, &stmt->forStmt.bodyCount);
        p->loop_depth--;
        return stmt;
    }
    else if (t.type == TOK_REPEAT) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_REPEAT;
        stmt->repeatStmt.count = parse_expr(p);
        p->loop_depth++;
        stmt->repeatStmt.body = parse_body(p, &stmt->repeatStmt.bodyCount);
        p->loop_depth--;
        return stmt;
    }
    else if (t.type == TOK_DO) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_DO_UNTIL;
        stmt->doUntilStmt.body = parse_body(p, &stmt->doUntilStmt.bodyCount);
        consume(p, TOK_UNTIL, "expected 'until'"); stmt->doUntilStmt.condition = parse_expr(p);
        return stmt;
    }
    else if (t.type == TOK_BREAK) {
        advance(p);
        Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_BREAK;
        stmt->breakStmt.label = NULL;
        if (peek(p).type == TOK_IDENT) {
            Token lt = consume(p, TOK_IDENT, "label after 'break'");
            stmt->breakStmt.label = sv_dup(lt.text);
        }
        return stmt;
    }
    else if (t.type == TOK_FUNC) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_FUNC;
        stmt->funcDef.name = consume(p, TOK_IDENT, "function name").text;
        consume(p, TOK_LPAREN, "'('");
        stmt->funcDef.params = NULL; stmt->funcDef.paramCount = 0;
        if (peek(p).type != TOK_RPAREN) {
            do {
                Token pn = consume(p, TOK_IDENT, "parameter name");
                stmt->funcDef.params = realloc(stmt->funcDef.params, (stmt->funcDef.paramCount + 1) * sizeof(StringView));
                stmt->funcDef.params[stmt->funcDef.paramCount++] = pn.text;
            if (!match(p, TOK_COMMA) || peek(p).type == TOK_RPAREN) break;
            } while (1);
        }
        consume(p, TOK_RPAREN, "')'");
        stmt->funcDef.body = parse_block(p, &stmt->funcDef.bodyCount);
        return stmt;
    }
    else if (t.type == TOK_RETURN) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_RETURN;
        if (peek(p).type == TOK_RBRACE || peek(p).type == TOK_EOF)
            stmt->returnStmt.value = NULL;
        else
            stmt->returnStmt.value = parse_expr(p);
        return stmt;
    }
    else if (t.type == TOK_MAIN) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_MAIN;
        stmt->mainStmt.flags = 0;
        while (peek(p).type == TOK_ENDLESS || peek(p).type == TOK_DAEMON || peek(p).type == TOK_RESTART || peek(p).type == TOK_SINGLE) {
            if (peek(p).type == TOK_ENDLESS) stmt->mainStmt.flags |= THREAD_FLAG_ENDLESS;
            else if (peek(p).type == TOK_DAEMON) stmt->mainStmt.flags |= THREAD_FLAG_DAEMON;
            else if (peek(p).type == TOK_RESTART) stmt->mainStmt.flags |= THREAD_FLAG_RESTART;
            else stmt->mainStmt.flags |= THREAD_FLAG_SINGLE;
            advance(p);
        }
        stmt->mainStmt.body = parse_block(p, &stmt->mainStmt.bodyCount);
        return stmt;
    }
    else if (t.type == TOK_GLOBAL) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_GLOBAL;
        stmt->globalStmt.names = NULL; stmt->globalStmt.nameCount = 0;
        do {
            Token name = consume(p, TOK_IDENT, "global variable name");
            stmt->globalStmt.names = realloc(stmt->globalStmt.names, (stmt->globalStmt.nameCount+1) * sizeof(StringView));
            stmt->globalStmt.names[stmt->globalStmt.nameCount++] = name.text;
        } while (match(p, TOK_COMMA));
        return stmt;
    }
    else if (t.type == TOK_THREAD || t.type == TOK_TASK) {
        /* trap: task/thread definitions inside loop bodies are silently ineffective (never executed); reject at compile time */
        if (p->loop_depth > 0) {
            if (g_err_json) { err_json("parse", p->lex.line, p->lex.col, "top-level", "task/thread", "definitions inside a loop are silently ineffective; define at top level"); exit(1); }
            fprintf(stderr, "Error at line %d: task/thread definitions inside a loop are silently ineffective; define at top level\n", p->lex.line);
            exit(1);
        }
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_THREAD_DEF;
        stmt->threadDef.flags = (t.type == TOK_TASK) ? THREAD_FLAG_TASK : 0;
        while (peek(p).type == TOK_ENDLESS || peek(p).type == TOK_DAEMON || peek(p).type == TOK_RESTART || peek(p).type == TOK_SINGLE) {
            if (peek(p).type == TOK_ENDLESS) stmt->threadDef.flags |= THREAD_FLAG_ENDLESS;
            else if (peek(p).type == TOK_DAEMON) stmt->threadDef.flags |= THREAD_FLAG_DAEMON;
            else if (peek(p).type == TOK_RESTART) stmt->threadDef.flags |= THREAD_FLAG_RESTART;
            else stmt->threadDef.flags |= THREAD_FLAG_SINGLE;
            advance(p);
        }
        stmt->threadDef.name = consume(p, TOK_IDENT, "thread name").text;
        match(p, TOK_COLON); /* 可选冒�? thread name: {} */
        stmt->threadDef.params = NULL; stmt->threadDef.paramCount = 0;
        if (match(p, TOK_LPAREN)) {
            if (peek(p).type != TOK_RPAREN) {
                do {
                    Token pn = consume(p, TOK_IDENT, "parameter name");
                    stmt->threadDef.params = realloc(stmt->threadDef.params, (stmt->threadDef.paramCount + 1) * sizeof(StringView));
                    stmt->threadDef.params[stmt->threadDef.paramCount++] = pn.text;
                if (!match(p, TOK_COMMA) || peek(p).type == TOK_RPAREN) break;
                } while (1);
            }
            consume(p, TOK_RPAREN, "')'");
        }
        match(p, TOK_COLON); /* 参数后可选冒�? thread name(n): {} */
        stmt->threadDef.body = parse_block(p, &stmt->threadDef.bodyCount);
        return stmt;
    }
    else if (t.type == TOK_START) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_START;
        stmt->startStmt.name = parse_qualified_name(p);
        stmt->startStmt.args = NULL; stmt->startStmt.argCount = 0;
        if (match(p, TOK_LPAREN)) {
            if (peek(p).type != TOK_RPAREN) {
                do {
                    Expr *a = parse_expr(p);
                    stmt->startStmt.args = realloc(stmt->startStmt.args, (stmt->startStmt.argCount + 1) * sizeof(Expr*));
                    stmt->startStmt.args[stmt->startStmt.argCount++] = a;
                } while (match(p, TOK_COMMA));
            }
            consume(p, TOK_RPAREN, "')'");
        }
        return stmt;
    }
    else if (t.type == TOK_PAUSE || t.type == TOK_RESUME || t.type == TOK_KILL) {
        int op = (t.type == TOK_PAUSE) ? THREAD_OP_PAUSE : (t.type == TOK_RESUME) ? THREAD_OP_RESUME : THREAD_OP_KILL;
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_THREAD_CTRL;
        stmt->threadCtrlStmt.op = op;
        if (peek(p).type == TOK_THIS) { advance(p); stmt->threadCtrlStmt.name = sv_from_cstr("this"); }
        else stmt->threadCtrlStmt.name = parse_qualified_name(p);
        return stmt;
    }
    else if (t.type == TOK_JOIN && peek_next(p).type != TOK_LPAREN) {
        /* 线程 join 语句（join(...) 是内置函数，走表达式分支�?*/
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_JOIN;
        stmt->joinStmt.name = parse_qualified_name(p);
        stmt->joinStmt.timeout = NULL;
        if (match(p, TOK_COMMA)) stmt->joinStmt.timeout = parse_expr(p);
        return stmt;
    }
    else if (t.type == TOK_IMPORT) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_IMPORT;
        stmt->importStmt.path = consume(p, TOK_STRING, "file path").text;
        if (match(p, TOK_AS)) stmt->importStmt.ns = consume(p, TOK_IDENT, "namespace name").text;
        else stmt->importStmt.ns = sv_from_cstr("");
        return stmt;
    }
    else if (t.type == TOK_LOCK || t.type == TOK_UNLOCK) {
        int is_unlock = (t.type == TOK_UNLOCK);
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_LOCK;
        stmt->lockStmt.name = consume(p, TOK_IDENT, "mutex name").text;
        if (is_unlock) { stmt->lockStmt.isBlock = 2; stmt->lockStmt.body = NULL; stmt->lockStmt.bodyCount = 0; }
        else if (peek(p).type == TOK_LBRACE) { stmt->lockStmt.isBlock = 1; stmt->lockStmt.body = parse_block(p, &stmt->lockStmt.bodyCount); }
        else { stmt->lockStmt.isBlock = 0; stmt->lockStmt.body = NULL; stmt->lockStmt.bodyCount = 0; }
        return stmt;
    }
    else if (t.type == TOK_SEND) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_SEND;
        stmt->sendStmt.name = parse_qualified_name(p);
        stmt->sendStmt.msg = parse_expr(p);
        return stmt;
    }
    else if (t.type == TOK_RECV) {
        advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_RECV;
        Expr *var = malloc(sizeof(Expr)); var->type = EXPR_IDENT;
        var->identName = consume(p, TOK_IDENT, "variable").text;
        stmt->recvStmt.target = var;
        stmt->recvStmt.timeout = NULL;
        if (match(p, TOK_COMMA)) stmt->recvStmt.timeout = parse_expr(p);
        return stmt;
    }
    else {
        Expr *expr = parse_expr(p);
        if (peek(p).type == TOK_EQ || peek(p).type == TOK_PLUS_EQ || peek(p).type == TOK_MINUS_EQ ||
            peek(p).type == TOK_STAR_EQ || peek(p).type == TOK_SLASH_EQ ||
            peek(p).type == TOK_PLUS_PLUS || peek(p).type == TOK_MINUS_MINUS) {
            InimerseTokenType op = peek(p).type; advance(p);
            Expr *val = NULL;
            if (op == TOK_PLUS_PLUS || op == TOK_MINUS_MINUS) {
                /* A2: x++ / x-- => x = x +1 / x = x -1 (simple variable only) */
                if (expr->type != EXPR_IDENT) {
                    fprintf(stderr, "Error: '++'/'--' currently only supported on simple variables\n");
                    exit(1);
                }
                Expr *one = malloc(sizeof(Expr)); one->type = EXPR_NUMBER; one->intVal = 1;
                Expr *bin = malloc(sizeof(Expr));
                bin->type = EXPR_BINARY;
                bin->binary.left = expr;
                bin->binary.op = (op == TOK_PLUS_PLUS) ? TOK_PLUS : TOK_MINUS;
                bin->binary.right = one;
                val = bin;
            } else {
                val = looks_like_set_start(p) ? parse_set_literal(p) : parse_expr(p);
                if (op != TOK_EQ) {
                /* x += v  =>  x = x + v */
                Expr *bin = malloc(sizeof(Expr));
                bin->type = EXPR_BINARY;
                bin->binary.left = expr;
                bin->binary.op = (op == TOK_PLUS_EQ) ? TOK_PLUS : (op == TOK_MINUS_EQ) ? TOK_MINUS :
                                 (op == TOK_STAR_EQ) ? TOK_STAR : TOK_SLASH;
                bin->binary.right = val;
                val = bin;
            }
            }
            Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_ASSIGN; RecordTag *tags = NULL; int tagCount =0;
            if (peek(p).type == TOK_LPAREN) { tagCount = parse_record_tags(p, &tags, 1); }
            stmt->assignStmt.target = expr; stmt->assignStmt.value = val; stmt->assignStmt.tags = tags; stmt->assignStmt.tagCount = tagCount; return stmt;
        }
        Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_EXPR; stmt->exprStmt.expr = expr; return stmt;
    }
}

static Stmt *parse_simple_stmt(Parser *p) {
    Token t = peek(p);
    if (t.type == TOK_USING) {
        advance(p);
        StringView mod;
        if (peek(p).type == TOK_THREAD) { advance(p); mod = sv_from_cstr("thread"); }
        else mod = consume(p, TOK_IDENT, "module name").text;
        Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_USING; stmt->usingStmt.modName = mod; return stmt;
    }
    else if (t.type == TOK_WINDOW) { advance(p); consume(p, TOK_LPAREN, "'('"); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_WINDOW; stmt->windowStmt.width = parse_expr(p); consume(p, TOK_COMMA, "','"); stmt->windowStmt.height = parse_expr(p); if (match(p, TOK_COMMA)) stmt->windowStmt.title = parse_expr(p); else stmt->windowStmt.title = NULL; consume(p, TOK_RPAREN, "')'"); return stmt; }
    else if (t.type == TOK_SHOW) { advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_SHOW; stmt->showStmt.imagePath = consume(p, TOK_STRING, "image path").text; if (match(p, TOK_AT)) { stmt->showStmt.x = parse_expr(p); consume(p, TOK_COMMA, "','"); stmt->showStmt.y = parse_expr(p); } else { stmt->showStmt.x = NULL; stmt->showStmt.y = NULL; } if (match(p, TOK_LAYER)) stmt->showStmt.layer = parse_expr(p); else stmt->showStmt.layer = NULL; if (peek(p).type == TOK_IDENT && sv_eq_cstr(peek(p).text, "with")) { advance(p); stmt->showStmt.tags = parse_with_clause(p, &stmt->showStmt.tagCount); } else { stmt->showStmt.tags = NULL; stmt->showStmt.tagCount = 0; } return stmt; }
    else if (t.type == TOK_HIDE) { advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_HIDE; stmt->hideStmt.imageExpr = parse_expr(p); return stmt; }
    else if (t.type == TOK_NEW) { advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_NEW; stmt->newStmt.prototype = parse_expr(p); if (match(p, TOK_AT)) { stmt->newStmt.x = parse_expr(p); consume(p, TOK_COMMA, "','"); stmt->newStmt.y = parse_expr(p); } else { stmt->newStmt.x = NULL; stmt->newStmt.y = NULL; } if (peek(p).type == TOK_IDENT && sv_eq_cstr(peek(p).text, "height")) { advance(p); stmt->newStmt.z = parse_expr(p); } else stmt->newStmt.z = NULL; if (peek(p).type == TOK_IDENT && sv_eq_cstr(peek(p).text, "with")) { advance(p); stmt->newStmt.tags = parse_with_clause(p, &stmt->newStmt.tagCount); } else { stmt->newStmt.tags = NULL; stmt->newStmt.tagCount = 0; } if (peek(p).type == TOK_LBRACE) stmt->newStmt.initStmts = parse_block(p, &stmt->newStmt.initCount); else { stmt->newStmt.initStmts = NULL; stmt->newStmt.initCount = 0; } return stmt; }
    else if (t.type == TOK_BLOCK) { advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_BLOCK_DEF; stmt->blockDef.name = consume(p, TOK_IDENT, "block name").text; consume(p, TOK_LBRACE, "'{'"); stmt->blockDef.propertyKeys = NULL; stmt->blockDef.propertyValues = NULL; stmt->blockDef.propCount = 0; stmt->blockDef.tags = NULL; stmt->blockDef.tagCount = 0; while (!match(p, TOK_RBRACE) && peek(p).type != TOK_EOF) { if (peek(p).type == TOK_IDENT && sv_eq_cstr(peek(p).text, "with")) { advance(p); Tag *new_tags = parse_with_clause(p, &stmt->blockDef.tagCount); if (new_tags) stmt->blockDef.tags = new_tags; } else if (peek(p).type == TOK_IDENT) { Token key = consume(p, TOK_IDENT, "prop"); consume(p, TOK_EQ, "'='"); Expr *val = parse_expr(p); stmt->blockDef.propertyKeys = realloc(stmt->blockDef.propertyKeys, (stmt->blockDef.propCount+1)*sizeof(StringView)); stmt->blockDef.propertyValues = realloc(stmt->blockDef.propertyValues, (stmt->blockDef.propCount+1)*sizeof(Expr*)); stmt->blockDef.propertyKeys[stmt->blockDef.propCount] = key.text; stmt->blockDef.propertyValues[stmt->blockDef.propCount] = val; stmt->blockDef.propCount++; } else break; } return stmt; }
    else if (t.type == TOK_CURSOR) { advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_CURSOR; stmt->cursorStmt.imagePath = parse_expr(p); return stmt; }
    else if (t.type == TOK_INCLUDE) { advance(p); Token file = consume(p, TOK_STRING, "file path"); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_INCLUDE; stmt->includeStmt.filename = file.text; return stmt; }
    else if (t.type == TOK_DELETE) { advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_DELETE; stmt->deleteStmt.target = parse_expr(p); return stmt; }
    else if (t.type == TOK_SAY) { advance(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_SAY; stmt->sayStmt.message = parse_expr(p); return stmt; }
    else if (t.type == TOK_STOP) {
        advance(p);
        if (match(p, TOK_ALL)) { Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_STOP; stmt->stopStmt.stopAll = true; return stmt; }
        Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_THREAD_CTRL; stmt->threadCtrlStmt.op = THREAD_OP_STOP;
        if (peek(p).type == TOK_THIS) { advance(p); stmt->threadCtrlStmt.name = sv_from_cstr("this"); }
        else stmt->threadCtrlStmt.name = consume(p, TOK_IDENT, "thread name").text;
        return stmt;
    }
    else if (t.type == TOK_INT || t.type == TOK_FLOAT || t.type == TOK_STR || t.type == TOK_BOOL) { advance(p); Token id = consume(p, TOK_IDENT, "variable name"); int tagCount = 0; Tag *tags = NULL; if (peek(p).type == TOK_IDENT && sv_eq_cstr(peek(p).text, "with")) { advance(p); tags = parse_with_clause(p, &tagCount); } Expr *value = NULL; if (match(p, TOK_EQ)) value = parse_expr(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_ASSIGN; Expr *var = malloc(sizeof(Expr)); var->type = EXPR_IDENT; var->identName = id.text; stmt->assignStmt.target = var; stmt->assignStmt.value = value; free(tags); return stmt; }
    else if (t.type == TOK_ARRAY) { advance(p); Token id = consume(p, TOK_IDENT, "array name"); if (match(p, TOK_EQ)) { Expr *value = parse_expr(p); Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_ASSIGN; Expr *var = malloc(sizeof(Expr)); var->type = EXPR_IDENT; var->identName = id.text; stmt->assignStmt.target = var; stmt->assignStmt.value = value; return stmt; } else { Stmt *stmt = malloc(sizeof(Stmt)); stmt->type = STMT_ASSIGN; Expr *var = malloc(sizeof(Expr)); var->type = EXPR_IDENT; var->identName = id.text; stmt->assignStmt.target = var; stmt->assignStmt.value = NULL; return stmt; } }
    else { parse_error_expected(p, "statement", peek(p)); return NULL; }
}

Program *parse_program(const char *source) {
    Parser p = {0}; lexer_init(&p.lex, source);
    Program *prog = malloc(sizeof(Program)); prog->stmts = NULL; prog->count = 0; int cap = 0;
    while (peek(&p).type != TOK_EOF) {
        while (peek(&p).type == TOK_SEMI) advance(&p);
        if (peek(&p).type == TOK_EOF) break;
        Stmt *s = parse_stmt(&p);
        if (prog->count >= cap) { cap = cap == 0 ? 8 : cap * 2; prog->stmts = realloc(prog->stmts, cap * sizeof(Stmt*)); }
        prog->stmts[prog->count++] = s;
    }
    return prog;
}

/* ==================== 多文�?import/include 支持 ==================== */
/* 单文件解析：import/include 语句原样保留�?AST 中，由编译器（compiler.c�?   在编译期递归解析（命名空间前缀�?+ 去重 + 环检测）�?   旧实现（拼接+rename_stmt）已废弃：AST 字符串改写穷举必漏，且无法支�?   嵌套命名空间 / 模块�?const-record-be / meta 属性名�?*/
Program *parse_program_file(const char *path) {
    char *src = inim_load_text(path);
    if (!src) return NULL;
    Program *prog = parse_program(src);
    /* �?free(src)：AST �?StringView 名字指向源码缓冲区，需保持到编译完�?       （泄漏量=脚本大小，每文件一次，可接受） */
    return prog;
}
