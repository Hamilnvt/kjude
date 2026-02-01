// TODO:
// - free dynamic arrays
// - parser:
//   > riguardare tutto il parser e farse funzioni piu' sensate, simili al lexer

// Grammar:
// statement  -> command ; statement | \eps
// command    -> var_decl | var_assign | f_call
// var_decl   -> "var" ident ( "=" expr )?
// var_assign -> ident "=" expr
// f_call     -> ident "(" f_args ")"
// expr       -> ident | int_lit | f_call | binop_expr
// binop_expr -> expr binop expr
// binop      -> "+"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <assert.h>
#include <ctype.h>
#include <time.h>

#define STRINGS_IMPLEMENTATION
#include "../libs/strings.h"

/// Begin Timing
struct timespec clock_start, clock_finish, clock_delta;
#define NS_PER_SECOND 1000000000

void time_from_here() { clock_gettime(CLOCK_MONOTONIC, &clock_start); }

void time_to_here()
{
    clock_gettime(CLOCK_MONOTONIC, &clock_finish);
    clock_delta.tv_nsec = clock_finish.tv_nsec - clock_start.tv_nsec;
    clock_delta.tv_sec  = clock_finish.tv_sec -  clock_start.tv_sec;
}

void time_print(char *msg)
{
    printf("INFO: %s took %d.%.9ld secs\n", msg, (int)clock_delta.tv_sec, clock_delta.tv_nsec);
}
/// End Timing

const char *print_definition = ""            \
    "print:\n"                               \
    "    mov     r9, -3689348814741910323\n" \
    "    sub     rsp, 40\n"                  \
    "    mov     BYTE [rsp+31], 10\n"        \
    "    lea     rcx, [rsp+30]\n"            \
    ".L2:\n"                                 \
    "    mov     rax, rdi\n"                 \
    "    lea     r8, [rsp+32]\n"             \
    "    mul     r9\n"                       \
    "    mov     rax, rdi\n"                 \
    "    sub     r8, rcx\n"                  \
    "    shr     rdx, 3\n"                   \
    "    lea     rsi, [rdx+rdx*4]\n"         \
    "    add     rsi, rsi\n"                 \
    "    sub     rax, rsi\n"                 \
    "    add     eax, 48\n"                  \
    "    mov     BYTE [rcx], al\n"           \
    "    mov     rax, rdi\n"                 \
    "    mov     rdi, rdx\n"                 \
    "    mov     rdx, rcx\n"                 \
    "    sub     rcx, 1\n"                   \
    "    cmp     rax, 9\n"                   \
    "    ja      .L2\n"                      \
    "    lea     rax, [rsp+32]\n"            \
    "    mov     edi, 1\n"                   \
    "    sub     rdx, rax\n"                 \
    "    xor     eax, eax\n"                 \
    "    lea     rsi, [rsp+32+rdx]\n"        \
    "    mov     rdx, r8\n"                  \
    "    mov     rax, 1\n"                   \
    "    syscall\n"                          \
    "    add     rsp, 40\n"                  \
    "    ret\n";

#define streq(s1, s2) (strcmp((s1), (s2)) == 0)

void todo(char *format, ...)
{
    char todo_buf[1024];
    va_list msg_fmt; 
    va_start(msg_fmt, format);
    vsprintf(todo_buf, format, msg_fmt);
    va_end(msg_fmt);
    printf("TODO: %s\n", todo_buf);
}

void error(char *format, ...)
{
    char err_buf[1024];
    va_list msg_fmt; 
    va_start(msg_fmt, format);
    vsprintf(err_buf, format, msg_fmt);
    va_end(msg_fmt);
    printf("ERROR: %s", err_buf);
}

void errorln(char *format, ...)
{
    char err_buf[1024];
    va_list msg_fmt; 
    va_start(msg_fmt, format);
    vsprintf(err_buf, format, msg_fmt);
    va_end(msg_fmt);
    printf("ERROR: %s\n", err_buf);
}

void note(char *format, ...)
{
    char note_buf[1024];
    va_list msg_fmt; 
    va_start(msg_fmt, format);
    vsprintf(note_buf, format, msg_fmt);
    va_end(msg_fmt);
    printf("NOTE: %s\n", note_buf);
}

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} ArrayOfCStrings;

typedef struct
{
    String *items;
    size_t count;
    size_t capacity;
} ArrayOfStrings;

ArrayOfCStrings included_files = {0};

typedef struct
{
    size_t row;
    size_t col;
    size_t file_path;
} Location;

char *loc_get_path(Location loc)
{
    char *path;
    da_get(included_files, loc.file_path, path);
    return path;
}

int loc_get_path_index(char *file_path)
{
    for (size_t i = 0; i < included_files.count; i++) {
        if (streq(file_path, included_files.items[i]))
            return i;
    }
    return -1;
}

Location loc_new(char *file_path)
{
    int index = loc_get_path_index(file_path);
    if (index == -1) {
        index = included_files.count;
        da_push(&included_files, strdup(file_path));
    } 
    return (Location){
        .row = 0,
        .col = 0,
        .file_path = index
    };
}

void loc_print(Location loc) { printf("%s:%zu:%zu", loc_get_path(loc), loc.row+1, loc.col+1); }

Location loc_clone(Location loc)
{
    return (Location){
        .row = loc.row,
        .col = loc.col,
        .file_path = loc.file_path
    };
}

// NOTE: copied from nob.h @ Tsoding
bool read_entire_file(const char *path, String *source)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)                 return false;
    if (fseek(f, 0, SEEK_END) < 0) { fclose(f); return false; }
    long m = ftell(f);
    if (m < 0)                     { fclose(f); return false; }
    if (fseek(f, 0, SEEK_SET) < 0) { fclose(f); return false; }

    size_t new_count = source->count + m;
    if (new_count > source->capacity) {
        source->items = realloc(source->items, new_count);
        assert(source->items != NULL && "Buy more RAM lool!!");
        source->capacity = new_count;
    }

    fread(source->items + source->count, m, 1, f);
    if (ferror(f)) {
        fclose(f);
        return false;
    }
    source->count = new_count;

    fclose(f);
    return true;
}

// Keywords
#define KW_VAR   "var"
#define KW_IF    "if"
#define KW_TRUE  "true"
#define KW_FALSE "false"

typedef enum
{
    TOK_NONE,
    TOK_IDENT,
    TOK_VAR,
    TOK_IF,
    TOK_TRUE,
    TOK_FALSE,
    TOK_INTEGER,
    TOK_L_PAREN,
    TOK_R_PAREN,
    TOK_L_SQPAREN,
    TOK_R_SQPAREN,
    TOK_L_CUPAREN,
    TOK_R_CUPAREN,
    TOK_SEMICOLON,
    TOK_OP_ASSIGN,
    TOK_OP_PLUS,
    TOK_TYPES_COUNT
} TokenType;

char *toktype_to_str(TokenType t)
{
    static_assert(TOK_TYPES_COUNT == 16, "Cover all token types in toktype_to_str");
    switch (t)
    {
        case TOK_IDENT:     return "Ident";
        case TOK_VAR:       return "Var";
        case TOK_IF:        return "If";
        case TOK_TRUE:      return "True";
        case TOK_FALSE:     return "False";
        case TOK_INTEGER:   return "Integer";
        case TOK_L_PAREN:   return "Lparen";
        case TOK_R_PAREN:   return "Rparen";
        case TOK_L_SQPAREN: return "Lsqparen";
        case TOK_R_SQPAREN: return "Rsqparen";
        case TOK_L_CUPAREN: return "Lcuparen";
        case TOK_R_CUPAREN: return "Rcuparen";
        case TOK_SEMICOLON: return "Semicolon";
        case TOK_OP_ASSIGN: return "OpAssign";
        case TOK_OP_PLUS:   return "OpPlus";
        case TOK_NONE:      return "None";
        default:
            errorln("unknown token type (%d)\n", t);
            exit(1);
    }
}

typedef struct
{
    TokenType type;
    char *text;
    Location loc;
    union { // Token value
        unsigned int val_uint; // TODO: assign it in lexing, non so bene se farlo in lexing o in parsing
    };
} Token;

Token tok_none() { return (Token){ .type = TOK_NONE }; }
void tok_print(Token tok) { printf("<%s, `%s`>", toktype_to_str(tok.type), tok.text); }

typedef struct
{
    Token *items;
    size_t count;
    size_t capacity;
} Tokens;

typedef struct
{
    ArrayOfStrings source;
    char c;
    Location loc;
} Lexer;

Lexer lex_new(char *file_path)
{
    Lexer lexer = {0};
    String full_source = {0};
    if (!read_entire_file(file_path, &full_source)) {
        fprintf(stderr, "Could not open file `%s`.\n", file_path);
        exit(1);
    }
    int i;
    String line = {0};
    da_for(full_source, i) {
        if (full_source.items[i] == '\n') {
            da_push(&lexer.source, s_clone(line));
            s_clear(&line);
        } else {
            s_push(&line, full_source.items[i]);
        }
    }

    if (false) {
        printf("\nSource code:\n");
        for (size_t i = 0; i < lexer.source.count; i++) {
            printf("%zu: `", i);
            s_print(lexer.source.items[i]);
            printf("`\n");
        }
        printf("\n");
    }

    lexer.loc = loc_new(file_path);
    da_free(&full_source);
    return lexer;
}

bool lex_is_empty(Lexer *lex) { return da_is_empty(&lex->source); }

String lex_curr_row(Lexer *lex) { return lex->source.items[lex->loc.row]; }
size_t lex_curr_row_count(Lexer *lex) { return lex_curr_row(lex).count; }
char lex_curr_char(Lexer *lex) { return lex_curr_row(lex).items[lex->loc.col]; }

bool lex_can_advance_row(Lexer *lex) { return lex->loc.row+1 < lex->source.count; }
bool lex_can_advance_col(Lexer *lex) { return lex->loc.col+1 < lex_curr_row_count(lex); }
bool lex_can_advance(Lexer *lex) { return lex_can_advance_row(lex) || lex_can_advance_col(lex); }

bool lex_advance(Lexer *lex)
{
    if (!lex_can_advance(lex)) return false;
    size_t row_len = lex_curr_row_count(lex);
    if (row_len == 0) {
        lex->loc.row++;
    } else {
        lex->loc.col = (lex->loc.col+1) % row_len;
        if (lex->loc.col == 0) lex->loc.row++;
    }
    return true;
}

bool lex_get(Lexer *lex)
{
    if (lex->loc.row < lex->source.count && lex->loc.col < lex_curr_row_count(lex)) {
        lex->c = lex_curr_char(lex);
        return true;
    } else if (lex_can_advance(lex)) {
        lex_advance(lex);
        return lex_get(lex);
    } else return false;
}

bool lex_peek(Lexer *lex)
{
    if (!lex_can_advance_col(lex)) return false;
    lex->c = lex_curr_row(lex).items[lex->loc.col+1];
    return true;
}

bool lex_expect(Lexer *lex, char xpctd)
{
    char tmp = lex->c;
    bool res = false;
    if (lex_peek(lex) && lex->c == xpctd) res = true;
    lex->c = tmp;
    return res;
}

bool lex_expect_digit(Lexer *lex)
{
    char tmp = lex->c;
    bool res = false;
    if (lex_peek(lex) && isdigit(lex->c)) res = true;
    lex->c = tmp;
    return res;
}

bool lex_expect_alpha(Lexer *lex)
{
    char tmp = lex->c;
    bool res = false;
    if (lex_peek(lex) && isalpha(lex->c)) res = true;
    lex->c = tmp;
    return res;
}

bool lex_expect_alphanum(Lexer *lex)
{
    char tmp = lex->c;
    bool res = false;
    if (lex_peek(lex) && isalnum(lex->c)) res = true;
    lex->c = tmp;
    return res;
}

bool lex_match(Lexer *lex, char xpctd)
{
    char tmp = lex->c;
    bool res = false;
    if (lex_peek(lex) && lex->c == xpctd) {
        lex_advance(lex);
        res = true;
    } else {
        lex->c = tmp;
        res = false;
    };
    return res;
}

// TODO: non sembra funzionare, ma forse non serve
bool lex_match_sequence(Lexer *lex, char *needle)
{
    printf("Siamo dentro\n");
    Location saved_loc = loc_clone(lex->loc);
    size_t len = strlen(needle);
    for (size_t i = 0; i < len; i++) {
        if (!lex_match(lex, needle[i])) {
            lex->loc = saved_loc;
            return false;
        }
    }
    return true;
}

Tokens lex_lex(Lexer *lex) // NOTE: assuming that it is correctly initialized
{
    static_assert(TOK_TYPES_COUNT == 16, "Cover all token types in lex_lex");
    Tokens tokens = {0};
    if (lex_is_empty(lex)) return tokens;
    String word = {0};
    char c;
    do {
        if (!lex_get(lex)) break;
        c = lex->c;
        if (isblank(c)) {
            continue;
        }
        Token token = (Token){
            .type = TOK_NONE,
            .loc  = loc_clone(lex->loc)
        };
               if (c == '=') { token.text = "="; token.type = TOK_OP_ASSIGN;
        } else if (c == '+') { token.text = "+"; token.type = TOK_OP_PLUS;
        } else if (c == '(') { token.text = "("; token.type = TOK_L_PAREN;
        } else if (c == ')') { token.text = ")"; token.type = TOK_R_PAREN;
        } else if (c == '[') { token.text = "["; token.type = TOK_L_SQPAREN;
        } else if (c == ']') { token.text = "]"; token.type = TOK_R_SQPAREN;
        } else if (c == '{') { token.text = "{"; token.type = TOK_L_CUPAREN;
        } else if (c == '}') { token.text = "}"; token.type = TOK_R_CUPAREN;
        } else if (c == ';') { token.text = ";"; token.type = TOK_SEMICOLON;
        } else if (c == '/') {
            if (lex_expect(lex, '/')) {
                lex->loc.col = -1; // effectively skip the line after the next lex_advance
                continue;
            } else {
                loc_print(lex->loc);
                printf(": ");
                errorln("Unexpected character '%c'", c);
                note("To comment put two of them: \"//\".");
                exit(1);
            }
        } else if (isdigit(c)) {
            s_push(&word, c);
            while (lex_expect_digit(lex)) {
                lex_advance(lex);
                lex_get(lex);
                s_push(&word, lex->c);
            }
            s_push_null(&word);
            token.text = strdup(word.items);
            s_clear(&word);

            token.type = TOK_INTEGER;
        } else if (isalpha(c) || c == '_') {
            s_push(&word, c);
            while (lex_expect_alphanum(lex) || lex_expect(lex, '_')) {
                lex_advance(lex);
                lex_get(lex);
                s_push(&word, lex->c);
            }
            s_push_null(&word);
            token.text = strdup(word.items);
            s_clear(&word);

            if      (streq(token.text, KW_VAR))   token.type = TOK_VAR; 
            else if (streq(token.text, KW_IF))    token.type = TOK_IF; 
            else if (streq(token.text, KW_TRUE))  token.type = TOK_TRUE; 
            else if (streq(token.text, KW_FALSE)) token.type = TOK_FALSE; 
            else                                  token.type = TOK_IDENT;
        } else {
            todo("lex '%c'", c);
            exit(1);
        }

        da_push(&tokens, token);
    } while (lex_advance(lex));
    return tokens;
}

typedef enum
{
    OP_NONE,
    OP_PRINT,
    OP_GLOB_VAR_ASSIGN,
    OP_LOAD16,
    OP_READ_GLOB_VAR,
    OP_TYPES_COUNT
} OpType;

typedef struct
{
    OpType type;
    union {
        unsigned int val_uint;
        char *val_str;
    };
} Op;

typedef struct
{
    Op *items;
    size_t count;
    size_t capacity;
} Ops;

typedef enum
{
    TYPE_INT,
    TYPES_COUNT
} VarType;

size_t size_of_type(VarType type)
{
    static_assert(TYPES_COUNT == 1, "Cover all types in size_of_type");
    switch (type)
    {
        case TYPE_INT: return 4;
        default: 
            errorln("Unreachable");
            exit(1);
    }
}

typedef struct
{
    int offset;  
    VarType type;
    char *name;
    Location loc;
} GlobVar;

typedef struct
{
    GlobVar *items;
    size_t count;
    size_t capacity;
} GlobalVars;
GlobalVars global_vars = {0};
size_t global_vars_total_offset = 0;

int global_var_index_by_name(char *var_name)
{
    int i; 
    da_for(global_vars, i) {
        if (streq(var_name, global_vars.items[i].name))
            return i;
    }
    return -1;
}

void global_vars_dump()
{
    printf("Global Variables:");
    if (da_is_empty(&global_vars)) printf(" empty\n");
    else {
        printf("\n");
        for (size_t i = 0; i < global_vars.count; i++) {
            printf("  %zu: %s\n", i, global_vars.items[i].name);
        }
    }
}

typedef struct
{
    Tokens tokens;
    size_t i;
} Parser;

Parser parser_new(Tokens tokens)
{
    return (Parser){
        .tokens = tokens,
        .i = 0
    };
}

bool parser_can_advance(Parser *p) { return p->i+1 < p->tokens.count; }
bool parser_advance(Parser *p)
{
    if (!parser_can_advance(p)) return false;
    p->i++;
    return true;
}

Token parser_get(Parser *p)
{
    if (p->i < p->tokens.count) return p->tokens.items[p->i];
    else return tok_none();
}

Token parser_peek(Parser *p)
{
    if (parser_can_advance(p)) return p->tokens.items[p->i+1];
    else return tok_none();
}

Token parser_next(Parser *p)
{
    if (parser_can_advance(p)) {
        p->i++;
        return parser_get(p);
    } else return tok_none();
}

bool parser_expect(Parser *p, TokenType xpctd) { return parser_peek(p).type == xpctd; }

bool parser_match(Parser *p, TokenType type)
{
    if (parser_expect(p, type)) {
        p->i++;
        return true;
    } else return false;
}

void error_expected_token_type(TokenType xpctd, Token victim, Token from)
{
    if (victim.type == TOK_NONE) {
        loc_print(from.loc);
        printf(": ");
        errorln("expecting `%s`, but got nothing instead.", toktype_to_str(xpctd));
    } else {
        loc_print(victim.loc);
        printf(": ");
        errorln("expecting `%s`, but got `%s` instead.", toktype_to_str(xpctd), toktype_to_str(victim.type));
    }
    exit(1);
}

void error_expected_token_types(TokenType *xpctd, int n, Token victim, Token from)
{
    if (victim.type == TOK_NONE) {
        loc_print(from.loc);
        printf(": ");
        error("expecting ");
        for (int i = 0; i < n; i ++) {
            if (i < n-1) printf(", ");
            else printf(" or ");
            printf("`%s`", toktype_to_str(xpctd[i]));
            printf(", but got nothing instead.\n");
        }
    } else {
        loc_print(victim.loc);
        printf(": ");
        error("expecting ");
        for (int i = 0; i < n; i ++) {
            if (i < n-1) printf(", ");
            else printf(" or ");
            printf("`%s`", toktype_to_str(xpctd[i]));
            printf(", but got `%s` instead.\n", toktype_to_str(victim.type));
        }
    }
    exit(1);
}

void parser_match_type_else_error(Parser *p, TokenType xpctd, Token from)
{
    if (!parser_match(p, xpctd)) {
        error_expected_token_type(xpctd, parser_get(p), from);
    }
}

size_t parser_match_types_else_error(Parser *p, TokenType *xpctd, size_t n, Token from)
{
    for (size_t i = 0; i < n; i++) {
        if (parser_match(p, xpctd[i])) return i;
    }
    error_expected_token_types(xpctd, n, parser_get(p), from);
    return 0;
}

void error_undeclared_variable(Token tok, char *msg)
{
    loc_print(tok.loc);
    printf(": ");
    error(msg);
    printf(" undeclared variable `%s`.\n", tok.text);
    exit(1);
}

Ops parser_parse(Parser *parser)
{
    Ops ops = {0};
    Token tok;
    static_assert(OP_TYPES_COUNT == 5, "Cover all op types in parser_parse");
    do {
        tok = parser_get(parser);
        if (tok.type == TOK_NONE) break;
        switch (tok.type)
        {
            case TOK_IDENT:
            {
                if (parser_match(parser , TOK_L_PAREN)) {
                    // TODO: parse args (every arg is an expr)
                    if (parser_expect(parser, TOK_INTEGER)) {
                        Token t_val = parser_next(parser);
                        Op op = {
                            .type = OP_LOAD16,
                            .val_uint = atoi(t_val.text)
                        };
                        da_push(&ops, op);
                    } else if (parser_expect(parser, TOK_IDENT)) {
                        Token t_var = parser_next(parser);
                        int var_i;
                        if ((var_i = global_var_index_by_name(t_var.text)) == -1) {
                            error_undeclared_variable(tok, "");
                        }
                        Op op = {
                            .type = OP_READ_GLOB_VAR,
                            .val_uint = var_i
                        };
                        da_push(&ops, op);
                    } else {
                        TokenType types[2] = {TOK_INTEGER, TOK_IDENT};
                        error_expected_token_types(types, 2, parser_get(parser), tok);
                    }
                    parser_match_type_else_error(parser, TOK_R_PAREN, tok);
                    parser_match_type_else_error(parser, TOK_SEMICOLON, tok);
                    if (streq(tok.text, "print")) {
                        Op op = { .type = OP_PRINT };
                        da_push(&ops, op);
                    } else {
                        todo("functions are not yet supported");
                        exit(1);
                    }
                    break;
                } else if (parser_expect(parser, TOK_OP_ASSIGN)) {
                    int var_i;
                    if ((var_i = global_var_index_by_name(tok.text)) == -1) {
                        error_undeclared_variable(tok, "trying to assign");
                    }
                    //da_push_many(op, parser_parse_expr(parser)); // TODO
                    Token t_val = parser_next(parser);
                    parser_match_type_else_error(parser, TOK_INTEGER, tok);
                    parser_match_type_else_error(parser, TOK_SEMICOLON, tok);
                    Op op = { .type = OP_LOAD16, .val_uint = atoi(t_val.text) }; // TODO: check for atoi corectness
                    da_push(&ops, op);
                    op = (Op){ .type = OP_GLOB_VAR_ASSIGN, .val_uint = var_i };
                    da_push(&ops, op);
                } else {
                    errorln("unknown word `%s`", tok.text);
                    exit(1);
                }
            } break;  
            case TOK_VAR:
            {
                parser_match_type_else_error(parser, TOK_IDENT, tok);
                Token t_var_name = parser_get(parser);
                int var_i;
                if ((var_i = global_var_index_by_name(t_var_name.text)) != -1) {
                    GlobVar var = global_vars.items[var_i]; 
                    loc_print(tok.loc);
                    printf(": ");
                    errorln("Redeclaration of variable `%s`.", var.name);
                    loc_print(var.loc);
                    printf(": ");
                    note(" Declared here the first time.");
                    exit(1);
                }
                var_i = global_vars.count;
                GlobVar var = (GlobVar){
                    .type = TYPE_INT,
                    .offset = global_vars_total_offset,
                    .name = strdup(t_var_name.text),
                    .loc = loc_clone(tok.loc)
                };
                global_vars_total_offset += size_of_type(var.type);
                da_push(&global_vars, var);
                if (parser_match(parser, TOK_SEMICOLON)) break;
                else if (parser_match(parser, TOK_OP_ASSIGN)) {
                    Token t_val = parser_get(parser); // TODO: che ci faccio con questo? Lo devo salvare in rax cosi' poi da metterlo nella variabile? Mi sfugge questa cosa.
                    parser_match_type_else_error(parser, TOK_INTEGER, tok); // TODO: per ora
                    parser_match_type_else_error(parser, TOK_SEMICOLON, t_val);
                    Op op = {
                        .type = OP_LOAD16,
                        .val_uint = atoi(t_val.text) // TODO: solo per ora, poi non sara' cosi'
                    };
                    da_push(&ops, op);
                    op = (Op){
                        .type = OP_GLOB_VAR_ASSIGN,
                        .val_uint = var_i
                    };
                    da_push(&ops, op);
                } else {
                    TokenType types[2] = {TOK_SEMICOLON, TOK_OP_ASSIGN};
                    error_expected_token_types(types, 2, parser_get(parser), tok);
                }
            } break;
            case TOK_IF:
            {
                Token t_condition = parser_get(parser);
                (void)t_condition;
                if (!(parser_match(parser, TOK_TRUE) || parser_match(parser, TOK_FALSE))) {
                    todo("parse if condition");
                    exit(1);
                }
                parser_match_type_else_error(parser, TOK_L_CUPAREN, tok);
                // TODO: parse if body (segnare da qualche parte, in uno stack magari, che si e' aperto un if/blocco e che si si aspetta venga chiuso ad un certo punto)
                // TODO: pensare all'op per if
            } break;
            case TOK_NONE:
            {
                fprintf(stderr, "Unreachable\n");
                exit(1);
            }
            default: 
                todo("parse token type %s", toktype_to_str(tok.type));
                exit(1);
        }
    } while (parser_advance(parser));
    return ops;
}

char *shift_arg(int *argc, char ***argv)
{
    if (*argc) {
        char *arg = *argv[0];
        (*argc)--;
        (*argv)++;
        return arg;
    } else {
        return NULL;
    }
}

void usage(FILE *stream, char *program_name)
{
    (void) program_name;
    (void) stream;
    todo("usage");
}

bool run_cmd(char **cmd, int len)
{
    if (cmd == NULL) return false;
    printf("CMD: ");
    for (int i = 0; i < len; i++) {
        printf("%s ", cmd[i]);
    }
    printf("\n");
    int status;
    switch (fork()) {
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);
        case 0:
            execvp(*cmd, cmd);
            fprintf(stderr, "ERROR: could not run cmd\n");
            exit(1);
        default:
            wait(&status);
            return WEXITSTATUS(status) == 0;
    }
}

int main(int argc, char **argv)
{
    char *program_name = shift_arg(&argc, &argv);
    assert((program_name != NULL) && "Program should be provided");

    if (argc < 1) {
        fprintf(stderr, "ERROR: file was not provided\n");
        usage(stderr, program_name);
        exit(1);
    }

    /// Begin Lexing
    char *file_path = shift_arg(&argc, &argv);
    // TODO: check for file extension
    time_from_here();
    Lexer lexer = lex_new(file_path);
    Tokens tokens = lex_lex(&lexer);
    time_to_here();
    time_print("Lexing");
    /// End Lexing

    if (false) {
        printf("\nTokens (%zu):\n", tokens.count);
        Token *t;
        da_foreach(tokens, t) {
            loc_print(t->loc);
            printf(": ");
            tok_print(*t);
            printf("\n");
        }
        printf("\n");
    }

    /// Begin Parsing
    time_from_here();

    Parser parser = parser_new(tokens);
    Ops ops = parser_parse(&parser);
    time_to_here();
    time_print("Parsing");
    /// End Parsing

    /// Begin Generating
    char *output_file = "output.asm"; // TODO: make it have the same name of the file_path and ending with .asm
    FILE *output = fopen(output_file, "w");
    if (output == NULL) {
        fprintf(stderr, "Could not open file `%s`\n", output_file);
        exit(1);
    }

    // TODO: beginning of file
    time_from_here();

    fprintf(output, "format ELF64 executable 3\n");
    fprintf(output, "segment readable executable\n");
    fprintf(output, "%s\n", print_definition);
    fprintf(output, "entry start\n");
    fprintf(output, "start:\n");
    fprintf(output, "    push rbp\n");
    fprintf(output, "    mov rbp, rsp\n");
    fprintf(output, "    sub rsp, %zu\n", 4*global_vars.count); // TODO: for now, all vars are int

    int i;
    da_for(ops, i) {
        Op op = ops.items[i];
        static_assert(OP_TYPES_COUNT == 5, "Cover all op types in code generation");
        switch (op.type)
        {
            case OP_PRINT:
            {
                fprintf(output, "    mov rdi, rax\n");
                fprintf(output, "    call print\n");
                break;
            }
            case OP_LOAD16:
            {
                fprintf(output, "    mov rax, %d\n", op.val_uint);
                break;
            }
            case OP_READ_GLOB_VAR:
            {
                fprintf(output, "    mov rax, [rbp-%d]\n", op.val_uint);
                break;
            }
            case OP_GLOB_VAR_ASSIGN:
            {
                fprintf(output, "    mov [rbp-%d], rax\n", op.val_uint);
                break;
            }
            case OP_NONE:
            default:
                fclose(output);
                fprintf(stderr, "Unreachable: op type %d\n", op.type);
                exit(1);
        }
    }

    // TODO: end file
    fprintf(output, "    mov rsp, rbp\n");
    fprintf(output, "    pop rbp\n");

    fprintf(output, "    mov rax, 60\n");
    fprintf(output, "    mov rdi, 0\n");
    fprintf(output, "    syscall\n");

    fclose(output);
    time_to_here();
    time_print("Generation");
    /// End Generating

    /// Begin Finalizing
    char *fasm_cmd[] = {"fasm", "-m", "524288", "output.asm", NULL}; // TODO: hardcoded
    run_cmd(fasm_cmd, 4);

    char *chmod_cmd[] = {"chmod", "+x", "output", NULL}; // TODO: hardcoded
    run_cmd(chmod_cmd, 3);
    /// End Finalizing

    return 0;
}
