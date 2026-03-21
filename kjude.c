// TODO:
// - write grammar

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
#include "strings.h"

#define INDEX_NOT_FOUND ((size_t)-1)

/// Begin Timing
struct timespec clock_start, clock_finish, clock_delta; // TODO make it a structure to measure multiple interleaving
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

static inline bool streq(const char *s1, const char *s2) { return strcmp((s1), (s2)) == 0; }

char *fmt_to_string(char *format, va_list fmt)
{
    va_list fmt_copy;
    va_copy(fmt_copy, fmt);
    size_t len = vsnprintf(NULL, 0, format, fmt_copy) + 1;
    char *string = malloc(sizeof(char)*len);
    if (!string) return NULL;
    vsnprintf(string, len, format, fmt);
    return string;
}

void todo(char *format, ...)
{
    va_list fmt;
    va_start(fmt, format);
    char *string = fmt_to_string(format, fmt);
    va_end(fmt);
    printf("TODO: %s\n", string);
    free(string);
}

void error(char *format, ...)
{
    va_list fmt;
    va_start(fmt, format);
    char *string = fmt_to_string(format, fmt);
    va_end(fmt);
    printf("ERROR: %s", string);
    free(string);
}

void errorln(char *format, ...)
{
    va_list fmt;
    va_start(fmt, format);
    char *string = fmt_to_string(format, fmt);
    va_end(fmt);
    printf("ERROR: %s\n", string);
    free(string);
}

void note(char *format, ...)
{
    va_list fmt;
    va_start(fmt, format);
    char *string = fmt_to_string(format, fmt);
    va_end(fmt);
    printf("NOTE: %s\n", string);
    free(string);
}

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} CStrings;

typedef struct
{
    String *items;
    size_t count;
    size_t capacity;
} Strings;

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
#define KEYWORD_LET   "let"
#define KEYWORD_IF    "if"
#define KEYWORD_FN    "fn"
#define KEYWORD_TRUE  "true"
#define KEYWORD_FALSE "false"

typedef enum
{
    TOK_NONE = 0,
    TOK_INVALID,
    TOK_IDENT,
    TOK_LET,
    TOK_IF,
    TOK_FN,
    TOK_TRUE,
    TOK_FALSE,
    TOK_INTEGER,
    TOK_STRING,
    TOK_L_PAREN,
    TOK_R_PAREN,
    TOK_L_SQPAREN,
    TOK_R_SQPAREN,
    TOK_L_CUPAREN,
    TOK_R_CUPAREN,
    TOK_COMMA,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_EQUALS,
    TOK_ARROW,
    TOK_PLUS,
    TOK_STAR,
    TOK_EOF,
    __tok_types_count
} TokenType;

typedef struct
{
    size_t row;
    size_t col;
    size_t file_path;
} Location;

typedef struct
{
    TokenType type;
    char lexeme[64];
    Location loc;
} Token;

static inline Token tok_none() { return (Token){ .type = TOK_NONE }; }

char *token_type_as_string(TokenType type)
{
    static_assert(__tok_types_count == 24, "Cover all token types in token_type_as_string");
    switch (type)
    {
        case TOK_NONE:      return "None";
        case TOK_INVALID:   return "Invalid";
        case TOK_IDENT:     return "Identifier";
        case TOK_LET:       return KEYWORD_LET;
        case TOK_IF:        return KEYWORD_IF;
        case TOK_FN:        return KEYWORD_FN;
        case TOK_TRUE:      return KEYWORD_TRUE;
        case TOK_FALSE:     return KEYWORD_FALSE;
        case TOK_INTEGER:   return "Integer";
        case TOK_STRING:    return "String";
        case TOK_EOF:       return "EOF";
        case TOK_L_PAREN:   return "(";
        case TOK_R_PAREN:   return ")";
        case TOK_L_SQPAREN: return "[";
        case TOK_R_SQPAREN: return "]";
        case TOK_L_CUPAREN: return "{";
        case TOK_R_CUPAREN: return "}";
        case TOK_COMMA:     return ",";
        case TOK_COLON:     return ":";
        case TOK_SEMICOLON: return ";";
        case TOK_EQUALS:    return "=";
        case TOK_ARROW:     return "->";
        case TOK_PLUS:      return "+";
        case TOK_STAR:      return "*";
        default:
            errorln("unknown token type (%d)\n", type);
            exit(1);
    }
}

static_assert(__tok_types_count == 24, "Cover all token types in token_print");
void token_print(Token tok)
{
    switch (tok.type) {
        case TOK_INVALID:
        case TOK_IDENT:
        case TOK_INTEGER:
        case TOK_STRING:
            printf("<%s, \"%s\">", token_type_as_string(tok.type), tok.lexeme);
            break;

        case TOK_NONE:
        case TOK_LET:
        case TOK_IF:
        case TOK_FN:
        case TOK_TRUE:
        case TOK_FALSE:
        case TOK_EOF:
            printf("<%s>", token_type_as_string(tok.type));
            break;

        case TOK_L_PAREN:
        case TOK_R_PAREN:
        case TOK_L_SQPAREN:
        case TOK_R_SQPAREN:
        case TOK_L_CUPAREN:
        case TOK_R_CUPAREN:
        case TOK_COMMA:
        case TOK_COLON:
        case TOK_SEMICOLON:
        case TOK_EQUALS:
        case TOK_ARROW:
        case TOK_PLUS:
        case TOK_STAR:
            printf("\"%s\"", tok.lexeme);
            break;

        default:
            fprintf(stderr, "Unreachable token type %d in token_print\n", tok.type);
            exit(1);
    }
}

typedef struct
{
    Token *items;
    size_t count;
    size_t capacity;
} Tokens;

typedef struct
{
    char *source;
    size_t pos;
    Location loc;
} Lexer;
static CStrings included_files = {0};

static inline char *loc_get_path(Location loc)
{
    if (loc.file_path >= included_files.count) return NULL;
    return included_files.items[loc.file_path];
}

size_t loc_get_path_index(char *file_path)
{
    for (size_t i = 0; i < included_files.count; i++) {
        if (streq(file_path, included_files.items[i]))
            return i;
    }
    return INDEX_NOT_FOUND;
}

Location loc_new(char *file_path)
{
    size_t index = loc_get_path_index(file_path);
    if (index == INDEX_NOT_FOUND) {
        index = included_files.count;
        da_push(&included_files, strdup(file_path));
    }
    return (Location){
        .row = 0,
        .col = 0,
        .file_path = index
    };
}

static inline void loc_print(Location loc)
{
    printf("%s:%zu.%zu: ", loc_get_path(loc), loc.row+1, loc.col+1);
}

typedef struct
{
    Tokens tokens;
    size_t pos;
} Parser;

// TODO: I want it to take the source code, it should not be responsible to open files and so I could lex strings in the code on the flight
Lexer lexer_new(char *file_path)
{
    Lexer lexer = {0};
    String source = {0};
    if (!read_entire_file(file_path, &source)) {
        fprintf(stderr, "Could not open file `%s`.\n", file_path);
        exit(1);
    }
    s_push_null(&source);
    da_fit(&source);
    lexer.source = source.items;

#if DEBUG
    printf("\nSource code:\n");
    printf("%s\n", lexer.source);
#endif // DEBUG

    lexer.loc = loc_new(file_path);
    return lexer;
}

//bool lexer_is_empty(Lexer *lex) { return da_is_empty(&lex->source); }
//
//String lexer_curr_row(Lexer *lex) { return lex->source.items[lex->loc.row]; }
//size_t lexer_curr_row_count(Lexer *lex) { return lexer_curr_row(lex).count; }
//char lexer_current_char(Lexer *lex) { return lexer_curr_row(lex).items[lex->loc.col]; }
//
//bool lexer_can_advance_row(Lexer *lex) { return lex->loc.row+1 < lex->source.count; }
//bool lexer_can_advance_col(Lexer *lex) { return lex->loc.col+1 < lexer_curr_row_count(lex); }
//bool lexer_can_advance(Lexer *lex) { return lexer_can_advance_row(lex) || lexer_can_advance_col(lex); }
//
//bool lexer_advance(Lexer *lex)
//{
//    if (!lexer_can_advance(lex)) return false;
//    size_t row_len = lexer_curr_row_count(lex);
//    if (row_len == 0) {
//        lex->loc.row++;
//    } else {
//        lex->loc.col = (lex->loc.col+1) % row_len;
//        if (lex->loc.col == 0) lex->loc.row++;
//    }
//    return true;
//}
//
//bool lexer_get(Lexer *lex)
//{
//    if (lex->loc.row < lex->source.count && lex->loc.col < lexer_curr_row_count(lex)) {
//        lex->c = lexer_current_char(lex);
//        return true;
//    } else if (lexer_can_advance(lex)) {
//        lexer_advance(lex);
//        return lexer_get(lex);
//    } else return false;
//}
//
//bool lexer_peek(Lexer *lex)
//{
//    if (!lexer_can_advance_col(lex)) return false;
//    lex->c = lexer_curr_row(lex).items[lex->loc.col+1];
//    return true;
//}
//
//bool lexer_expect(Lexer *lex, char expected)
//{
//    char tmp = lex->c;
//    bool res = false;
//    if (lexer_peek(lex) && lex->c == expected) res = true;
//    lex->c = tmp;
//    return res;
//}
//
//bool lexer_expect_digit(Lexer *lex)
//{
//    char tmp = lex->c;
//    bool res = false;
//    if (lexer_peek(lex) && isdigit(lex->c)) res = true;
//    lex->c = tmp;
//    return res;
//}
//
//bool lexer_expect_alpha(Lexer *lex)
//{
//    char tmp = lex->c;
//    bool res = false;
//    if (lexer_peek(lex) && isalpha(lex->c)) res = true;
//    lex->c = tmp;
//    return res;
//}
//
//bool lexer_expect_alphanum(Lexer *lex)
//{
//    char tmp = lex->c;
//    bool res = false;
//    if (lexer_peek(lex) && isalnum(lex->c)) res = true;
//    lex->c = tmp;
//    return res;
//}
//
//bool lexer_match(Lexer *lex, char expected)
//{
//    char tmp = lex->c;
//    bool res = false;
//    if (lexer_peek(lex) && lex->c == expected) {
//        lexer_advance(lex);
//        res = true;
//    } else {
//        lex->c = tmp;
//        res = false;
//    };
//    return res;
//}
//
//// TODO: non sembra funzionare, ma forse non serve
//bool lexer_match_sequence(Lexer *lex, char *needle)
//{
//    printf("Siamo dentro\n");
//    Location saved_loc = loc_clone(lex->loc);
//    size_t len = strlen(needle);
//    for (size_t i = 0; i < len; i++) {
//        if (!lexer_match(lex, needle[i])) {
//            lex->loc = saved_loc;
//            return false;
//        }
//    }
//    return true;
//}

// TODO:
// - advance location
// - create utility macros/functions
static inline char lexer_current_char(const Lexer *lexer) { return lexer->source[lexer->pos]; }
#define current_char (lexer_current_char(lexer))

void lexer_eat_spaces(Lexer *lexer)
{
    char c;
    while (isspace(c = current_char)) {
        lexer->pos++;
        if (c == '\n') {
            lexer->loc.row++;
            lexer->loc.col = 0;
        } else lexer->loc.col++;
    }
}

void lexer_eat_comment(Lexer *lexer)
{
    char c;
    while (true) {
        c = current_char;
        if (c == '\0' || c == '\n') break;
        lexer->pos++;
    }
    if (c == '\n') {
        lexer->pos++;
        lexer->loc.row++;
        lexer->loc.col = 0;
    }
}

static inline bool can_start_ident(char c) { return isalpha(c) || c == '_'; }
static inline bool can_continue_ident(char c) { return isalnum(c) || c == '_'; }

static inline bool can_start_number(char c) { return isdigit(c); }
static inline bool can_continue_number(char c) { return isdigit(c); }

static_assert(__tok_types_count == 24, "Cover all token types in lexer_get_next_token");
Token lexer_get_next_token(Lexer *lexer)
{
    lexer_eat_spaces(lexer);

    Token token = { .loc = lexer->loc };

    char c = current_char;

    if (c == '\0') {
        token.type = TOK_EOF;
    } else if (can_start_ident(c)) {
        int i = 0;
        do {
            token.lexeme[i++] = c;
            lexer->pos++;
            lexer->loc.col++;
            c = current_char;
        } while (can_continue_ident(c));
        token.lexeme[i] = '\0';
        
             if (streq(token.lexeme, KEYWORD_LET))   token.type = TOK_LET;
        else if (streq(token.lexeme, KEYWORD_IF))    token.type = TOK_IF;
        else if (streq(token.lexeme, KEYWORD_FN))    token.type = TOK_FN;
        else if (streq(token.lexeme, KEYWORD_TRUE))  token.type = TOK_TRUE;
        else if (streq(token.lexeme, KEYWORD_FALSE)) token.type = TOK_FALSE;
        else                                         token.type = TOK_IDENT;
    } else if (can_start_number(c)) {
        int i = 0;
        do {
            token.lexeme[i++] = c;
            lexer->pos++;
            lexer->loc.col++;
            c = current_char;
        } while (can_continue_number(c));
        token.lexeme[i] = '\0';
        token.type = TOK_INTEGER;
    } else {
        int i = 0;
        token.lexeme[i++] = c;
        lexer->pos++;
        lexer->loc.col++;

        switch (c) {
            case ',': token.type = TOK_COMMA;     break;
            case ':': token.type = TOK_COLON;     break;
            case ';': token.type = TOK_SEMICOLON; break;
            case '=': token.type = TOK_EQUALS;    break;
            case '+': token.type = TOK_PLUS;      break;
            case '-': {
                if (current_char == '>') {
                    token.type = TOK_ARROW;
                    token.lexeme[i++] = current_char;
                    lexer->pos++;
                    lexer->loc.col++;
                }
            } break;
            case '*': token.type = TOK_STAR;      break;
            case '/': {
                if (current_char == '/') {
                    lexer_eat_comment(lexer);
                    return lexer_get_next_token(lexer);
                }
            } break;
            case '(': token.type = TOK_L_PAREN;   break;
            case ')': token.type = TOK_R_PAREN;   break;
            case '[': token.type = TOK_L_SQPAREN; break;
            case ']': token.type = TOK_R_SQPAREN; break;
            case '{': token.type = TOK_L_CUPAREN; break;
            case '}': token.type = TOK_R_CUPAREN; break;
            case '"': {
                i = 0;
                c = current_char;
                while (c != '\0' && c != '"') {
                    token.lexeme[i++] = c;
                    lexer->pos++;
                    lexer->loc.col++;
                    c = current_char;
                }
                if (c == '\0') {
                    loc_print(token.loc);
                    errorln("Unclosed string");
                    exit(1);
                }
                token.type = TOK_STRING;
                lexer->pos++;
                lexer->loc.col++;
            } break;
            default: token.type = TOK_INVALID; break;
        }

        token.lexeme[i] = '\0';
    }

    return token;
}

Tokens lexer_lex(Lexer *lexer)
{
    Tokens tokens = {0};
    Token token;
    do {
        token = lexer_get_next_token(lexer);
        da_push(&tokens, token);
    } while (token.type != TOK_EOF);
    return tokens;
}

//Tokens lexer_lex(Lexer *lex) // NOTE: assuming that it is correctly initialized
//{
//    static_assert(__tok_types_count == 16, "Cover all token types in lexer_lex");
//    Tokens tokens = {0};
//    if (lexer_is_empty(lex)) return tokens;
//    String word = {0};
//    char c;
//    do {
//        if (!lexer_get(lex)) break;
//        c = lex->c;
//        if (isblank(c)) {
//            continue;
//        }
//        Token token = (Token){
//            .type = TOK_NONE,
//            .loc  = loc_clone(lex->loc)
//        };
//        token.lexeme[1] = '\0';
//               if (c == '=') { token.lexeme[0] = '='; token.type = TOK_EQUALS;
//        } else if (c == '+') { token.lexeme[0] = '+'; token.type = TOK_PLUS;
//        } else if (c == '(') { token.lexeme[0] = '('; token.type = TOK_L_PAREN;
//        } else if (c == ')') { token.lexeme[0] = ')'; token.type = TOK_R_PAREN;
//        } else if (c == '[') { token.lexeme[0] = '['; token.type = TOK_L_SQPAREN;
//        } else if (c == ']') { token.lexeme[0] = ']'; token.type = TOK_R_SQPAREN;
//        } else if (c == '{') { token.lexeme[0] = '{'; token.type = TOK_L_CUPAREN;
//        } else if (c == '}') { token.lexeme[0] = '}'; token.type = TOK_R_CUPAREN;
//        } else if (c == ';') { token.lexeme[0] = ';'; token.type = TOK_SEMICOLON;
//        } else if (c == '/') {
//            if (lexer_expect(lex, '/')) {
//                lex->loc.col = -1; // NOTE: effectively skip the line after the next lexer_advance
//                continue;
//            } else {
//                loc_print(lex->loc);
//                printf(": ");
//                errorln("Unexpected character '%c'", c);
//                note("To comment put two of them: \"//\".");
//                exit(1);
//            }
//        } else if (isdigit(c)) {
//            s_push(&word, c);
//            while (lexer_expect_digit(lex)) {
//                lexer_advance(lex);
//                lexer_get(lex);
//                s_push(&word, lex->c);
//            }
//            s_push_null(&word);
//            strncpy(token.lexeme, word.items, word.count);
//            s_clear(&word);
//
//            token.type = TOK_INTEGER;
//        } else if (isalpha(c) || c == '_') {
//            s_push(&word, c);
//            while (lexer_expect_alphanum(lex) || lexer_expect(lex, '_')) {
//                lexer_advance(lex);
//                lexer_get(lex);
//                s_push(&word, lex->c);
//            }
//            s_push_null(&word);
//            strncpy(token.lexeme, word.items, word.count);
//            s_clear(&word);
//
//            if      (streq(token.lexeme, KW_LET))   token.type = TOK_LET; 
//            else if (streq(token.lexeme, KW_IF))    token.type = TOK_IF; 
//            else if (streq(token.lexeme, KW_TRUE))  token.type = TOK_TRUE; 
//            else if (streq(token.lexeme, KW_FALSE)) token.type = TOK_FALSE; 
//            else                                  token.type = TOK_IDENT;
//        } else {
//            todo("lex '%c'", c);
//            exit(1);
//        }
//
//        da_push(&tokens, token);
//    } while (lexer_advance(lex));
//    return tokens;
//}

typedef enum
{
    ASTNODE_PROGRAM,
    ASTNODE_VAR_DECL,
    ASTNODE_VAR_ASSIGN,
    ASTNODE_BLOCK,
    ASTNODE_EXPRESSION_STATEMENT,
    ASTNODE_IF,
    ASTNODE_FN_DECL,
    ASTNODE_CALL,
    ASTNODE_BINOP,
    ASTNODE_NUMBER,
    ASTNODE_STRING,
    ASTNODE_BOOL,
    ASTNODE_IDENT,
    __astnode_types_count
} ASTNodeType;

typedef enum
{
    OP_PLUS,
    OP_STAR,
    __op_types_count,
} Operator;

Operator operator_from_token_type(TokenType type)
{
    if (type == TOK_PLUS) return OP_PLUS;
    if (type == TOK_STAR) return OP_STAR;
    assert(0 && "Unreachable");
}

char *operator_as_string(Operator op)
{
    switch (op) {
    case OP_PLUS: return "+";
    case OP_STAR: return "*";
    default:
        printf("Unreachable operator %u in operator_as_string\n", op);
        exit(1);
    }
}

typedef struct ASTNode
{
    ASTNodeType type;
    union {
        char string_value[64];
        int32_t int_value;
        bool bool_value;
        Operator op;
    };
    struct ASTNode* left;
    struct ASTNode* right;
    struct ASTNode* next;
} ASTNode;

ASTNode* create_node(ASTNodeType type)
{
    ASTNode* node = malloc(sizeof(ASTNode));
    *node = (ASTNode){ .type = type };
    return node;
}

//typedef enum
//{
//    OP_VAR_ASSIGN,
//    OP_VAR_READ,
//    __op_types_count
//} OpType;
//
//const char *op_type_to_string(OpType type)
//{
//    switch (type)
//    {
//    case OP_VAR_READ:   return "VAR_READ";
//    case OP_VAR_ASSIGN: return "VAR_ASSIGN";
//
//    default: {
//        fprintf(stderr, "Unreachable op type %d in op_type_to_string\n", type);
//        exit(1);
//    }
//    }
//}

typedef ASTNode* (*PrefixExprFn)(Parser *);
typedef ASTNode* (*InfixExprFn)(Parser *, ASTNode *left);

typedef enum {
    PREC_NONE   = 0,
    PREC_TERM   = 10, // +
    PREC_FACTOR = 20  // *
} Precedence;

typedef struct {
    PrefixExprFn prefix;
    InfixExprFn infix;
    Precedence precedence;
} ExprRule;

static inline Parser parser_new(Tokens tokens) { return (Parser){ .tokens = tokens }; }
static inline void parser_advance(Parser *p) { p->pos++; }
static inline Token *parser_current_token(Parser *p){ return da_get_ptr(p->tokens, p->pos); }
#define current_token (parser_current_token(parser))

Token *parser_peek(Parser *p)
{
    if (p->pos + 1 >= p->tokens.count) return NULL;
    else return &p->tokens.items[p->pos + 1];
}

void parser_expect(Parser *parser, TokenType type)
{
    Token *token = current_token;
    if (token->type == type) parser_advance(parser);
    else {
        loc_print(current_token->loc);
        error("Expecting token \"%s\", but got token ", token_type_as_string(type));
        token_print(*token);
        printf("\n");
        exit(1);
    }
}

static inline ExprRule* get_parse_rule(TokenType type)
{
    extern ExprRule expression_rules[];
    assert(type < __tok_types_count);
    return &expression_rules[type];
}

ASTNode* parse_number(Parser *parser)
{
    ASTNode* node = create_node(ASTNODE_NUMBER);
    node->int_value = atoi(current_token->lexeme);
    parser_advance(parser);
    return node;
}

ASTNode* parse_string(Parser *parser)
{
    ASTNode* node = create_node(ASTNODE_STRING);
    strcpy(node->string_value, current_token->lexeme);
    parser_advance(parser);
    return node;
}

ASTNode *parse_expression_with_precedence(Parser *parser, Precedence precedence)
{
    PrefixExprFn prefix_rule = get_parse_rule(current_token->type)->prefix;
    if (!prefix_rule) return NULL;

    ASTNode *expr = prefix_rule(parser);
    
    while (precedence < get_parse_rule(current_token->type)->precedence) {
        InfixExprFn infix_rule = get_parse_rule(current_token->type)->infix;
        expr = infix_rule(parser, expr);
    }
    
    return expr;
}

static inline ASTNode *parse_expression(Parser *parser) { return parse_expression_with_precedence(parser, PREC_NONE); }

ASTNode* parse_grouping(Parser *parser)
{
    parser_advance(parser);
    ASTNode* node = parse_expression(parser);
    if (!node) {
        loc_print(current_token->loc);
        error("Expected expression, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }
    if (current_token->type != TOK_R_PAREN) {
        printf("Syntax Error: Expected ')' after expression, found '%s'\n", current_token->lexeme); // TODO
        exit(1);
    }
    parser_advance(parser);
    return node;
}

ASTNode *parse_var_decl(Parser *parser)
{
    parser_advance(parser); // "let"
    ASTNode *node = create_node(ASTNODE_VAR_DECL);

    if (current_token->type != TOK_IDENT) {
        loc_print(current_token->loc);
        error("Expected variable name, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }

    strcpy(node->string_value, current_token->lexeme);
    parser_advance(parser); // varname

    parser_expect(parser, TOK_EQUALS);
    node->left = parse_expression(parser);
    if (!node->left) {
        loc_print(current_token->loc);
        error("Expected expression, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }

    parser_expect(parser, TOK_SEMICOLON);
    return node;
}

ASTNode *parse_statement(Parser *parser);

ASTNode *parse_block(Parser *parser)
{
    parser_advance(parser); // '{'

    ASTNode *root = create_node(ASTNODE_BLOCK);

    Token *next_token = parser_peek(parser);
    if (!next_token) {
        printf("ERROR: TODO %u\n", __LINE__);
        exit(1);
    }
    if (next_token->type == TOK_R_CUPAREN) { // empty block
        parser_advance(parser); // '}'
        return root;
    }

    root->left = parse_statement(parser); 
    if (root->left) {
        ASTNode* current_node = root->left;
        while (current_token->type != TOK_R_CUPAREN) {
            if (current_token->type == TOK_EOF) {
                printf("ERROR: TODO %u\n", __LINE__);
                exit(1);
            }
            current_node->next = parse_statement(parser);
            if (current_node->next) current_node = current_node->next;
        }
    }
    parser_expect(parser, TOK_R_CUPAREN);
    
    return root;
}

ASTNode *parse_fn_decl(Parser *parser)
{
    parser_advance(parser); // "fn"
    ASTNode *node = create_node(ASTNODE_FN_DECL);

    if (current_token->type != TOK_IDENT) {
        loc_print(current_token->loc);
        error("Expected function name, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }

    strcpy(node->string_value, current_token->lexeme);
    parser_advance(parser); // function name

    parser_expect(parser, TOK_COLON);

    parser_expect(parser, TOK_L_PAREN); // TODO: for now, then I will have to parse the parameters list
    parser_expect(parser, TOK_R_PAREN);

    if (current_token->type == TOK_ARROW) {
        parser_expect(parser, TOK_IDENT); // return type
    }

    parser_expect(parser, TOK_EQUALS);
    node->left = parse_block(parser);

    return node;
}

ASTNode *parse_true(Parser *parser)
{
    ASTNode* node = create_node(ASTNODE_BOOL);
    node->bool_value = true;
    parser_advance(parser);
    return node;
}
ASTNode *parse_false(Parser *parser)
{
    ASTNode* node = create_node(ASTNODE_BOOL);
    node->bool_value = false;
    parser_advance(parser);
    return node;
}

ASTNode *parse_if(Parser *parser)
{
    parser_advance(parser); // "if"

    ASTNode *node = create_node(ASTNODE_IF);
    node->left = parse_expression(parser);
    if (!node->left) {
        loc_print(current_token->loc);
        error("Expected expression, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }
    if (current_token->type == TOK_L_CUPAREN) {
        node->right = parse_block(parser);
    } else if (current_token->type == TOK_COLON) {
        parser_advance(parser); // ':'
        ASTNode *block = create_node(ASTNODE_BLOCK);
        block->left = parse_statement(parser);
        node->right = block;
    } else {
        loc_print(current_token->loc);
        error("Expecting block or ':', but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }
    return node;
}

ASTNode *parse_expression_list(Parser *parser)
{
    ASTNode *list = parse_expression(parser);
    if (!list) return NULL;
    ASTNode *elt = list;
    do {
        if (current_token->type != TOK_COMMA) break;
        parser_advance(parser); // ','
        elt->next = parse_expression(parser);
        elt = elt->next;
    } while (elt);
    return list;
}

ASTNode *parse_call(Parser *parser)
{
    ASTNode *node = create_node(ASTNODE_CALL);

    Token *called = current_token;
    strcpy(node->string_value, called->lexeme);
    parser_advance(parser); // called

    parser_advance(parser); // '('
    node->left = parse_expression_list(parser);
    parser_expect(parser, TOK_R_PAREN);

    return node;
}

ASTNode* parse_ident(Parser *parser)
{
    Token *next = parser_peek(parser); 
    if (next && next->type == TOK_L_PAREN) return parse_call(parser);

    ASTNode* node = create_node(ASTNODE_IDENT);
    strcpy(node->string_value, current_token->lexeme);
    parser_advance(parser);
    return node;
}

ASTNode *parse_var_assignment(Parser *parser)
{
    ASTNode *node = create_node(ASTNODE_VAR_ASSIGN);
    strcpy(node->string_value, current_token->lexeme);
    parser_advance(parser);
    parser_expect(parser, TOK_EQUALS);
    node->left = parse_expression(parser);
    if (!node->left) {
        loc_print(current_token->loc);
        error("Expected expression, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }
    parser_expect(parser, TOK_SEMICOLON);
    return node;
}

ASTNode *parse_binary(Parser *parser, ASTNode *left)
{
    Token *op_token = current_token;
    ASTNode* node = create_node(ASTNODE_BINOP);
    node->op = operator_from_token_type(op_token->type);
    parser_advance(parser);
    ExprRule *rule = get_parse_rule(op_token->type);
    node->left = left;
    node->right = parse_expression_with_precedence(parser, rule->precedence);
    if (!node->right) {
        loc_print(current_token->loc);
        error("Expected expression, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }
    return node;
}

static_assert(__tok_types_count == 24, "Cover all token types in expression_rules table");
ExprRule expression_rules[__tok_types_count] = {
    [TOK_NONE]       = {NULL, NULL, PREC_NONE},
    [TOK_INVALID]    = {NULL, NULL, PREC_NONE},
    [TOK_LET]        = {NULL, NULL, PREC_NONE},
    [TOK_IF]         = {NULL, NULL, PREC_NONE},
    [TOK_FN]         = {NULL, NULL, PREC_NONE},
    [TOK_L_SQPAREN]  = {NULL, NULL, PREC_NONE},
    [TOK_R_SQPAREN]  = {NULL, NULL, PREC_NONE},
    [TOK_L_CUPAREN]  = {NULL, NULL, PREC_NONE},
    [TOK_R_CUPAREN]  = {NULL, NULL, PREC_NONE},
    [TOK_COMMA]      = {NULL, NULL, PREC_NONE},
    [TOK_COLON]      = {NULL, NULL, PREC_NONE},
    [TOK_SEMICOLON]  = {NULL, NULL, PREC_NONE},
    [TOK_EQUALS]     = {NULL, NULL, PREC_NONE},
    [TOK_ARROW]      = {NULL, NULL, PREC_NONE},
    [TOK_EOF]        = {NULL, NULL, PREC_NONE},

    [TOK_IDENT]   = {parse_ident,    NULL,         PREC_NONE},
    [TOK_INTEGER] = {parse_number,   NULL,         PREC_NONE},
    [TOK_STRING]  = {parse_string,   NULL,         PREC_NONE},
    [TOK_TRUE]    = {parse_true,     NULL,         PREC_NONE},
    [TOK_FALSE]   = {parse_false,    NULL,         PREC_NONE},

    [TOK_L_PAREN] = {parse_grouping, NULL,         PREC_NONE},
    [TOK_R_PAREN] = {NULL,           NULL,         PREC_NONE},

    [TOK_PLUS]    = {NULL,           parse_binary, PREC_TERM},
    [TOK_STAR]    = {NULL,           parse_binary, PREC_FACTOR},
};

typedef enum
{
    VAR_GLOBAL,
    //VAR_LOCAL,
    __variable_kinds_count
} VariableKind;

typedef union
{
    int32_t i32;
} KjudeValue;

//typedef struct
//{
//    OpType type;
//    union {
//        struct { // Vars
//            VariableKind var_kind;
//            size_t var_index;
//            KjudeValue value; // OP_GLOB_VAR_ASSIGN
//        };
//    };
//} Op;

//typedef struct
//{
//    Op *items;
//    size_t count;
//    size_t capacity;
//} Ops;

typedef enum
{
    TYPE_I32,
    __types_count
} KjudeType;

size_t size_of_type(KjudeType type)
{
    static_assert(__types_count == 1, "Cover all types in size_of_type");
    switch (type)
    {
        case TYPE_I32: return sizeof(int32_t);
        default: 
            errorln("Unreachable");
            exit(1);
    }
}

typedef struct
{
    size_t index;  
    VariableKind kind;
    char *name;
    Location loc;
    KjudeType type;
    bool initialized;
    KjudeValue value;
} Variable;

typedef struct
{
    Variable *items;
    size_t count;
    size_t capacity;
} Variables;
Variables global_vars = {0};

size_t global_var_index_by_name(char *var_name)
{
    da_enumerate (global_vars, i, var) {
        if (streq(var_name, var->name))
            return i;
    }
    return INDEX_NOT_FOUND;
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

ASTNode *parse_expression_statement(Parser *parser)
{
    ASTNode *node = create_node(ASTNODE_EXPRESSION_STATEMENT);
    node->left = parse_expression(parser);
    if (!node->left) {
        loc_print(current_token->loc);
        error("Expected expression, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }
    parser_expect(parser, TOK_SEMICOLON);
    return node;
}

ASTNode *parse_statement(Parser *parser)
{
    ASTNode *node = NULL;
    Token *token = current_token;
    switch (token->type) {
        case TOK_LET: {
            node = parse_var_decl(parser);
        } break;

        case TOK_IF: {
            node = parse_if(parser);
        } break;

        case TOK_FN: {
            node = parse_fn_decl(parser);
        } break;

        case TOK_IDENT: {
            Token *next = parser_peek(parser);
            if (next && next->type == TOK_EQUALS) {
                node = parse_var_assignment(parser);
            } else {
                node = parse_expression_statement(parser);
            }
        } break;

        default:
            if (token->type < __tok_types_count) {
                node = parse_expression_statement(parser);
            } else {
                printf("Unreachable token "); token_print(*current_token); printf(" in parse_statement\n");
                exit(1);
            }
    }

    return node;
}

ASTNode *parse_program(Parser *parser)
{
    ASTNode* ast = create_node(ASTNODE_PROGRAM);
    ASTNode* node = ast;

    while (current_token->type != TOK_EOF) {
        node->next = parse_statement(parser);
        if (node->next) node = node->next;
    }
    
    return ast;
}

//bool parser_can_advance(Parser *p) { return p->i+1 < p->tokens.count; }
//bool parser_advance(Parser *p)
//{
//    if (!parser_can_advance(p)) return false;
//    p->i++;
//    return true;
//}
//
//Token parser_get(Parser *p)
//{
//    if (p->i < p->tokens.count) return p->tokens.items[p->i];
//    else return tok_none();
//}
//
//Token parser_peek(Parser *p)
//{
//    if (parser_can_advance(p)) return p->tokens.items[p->i+1];
//    else return tok_none();
//}
//
//Token parser_next(Parser *p)
//{
//    if (parser_can_advance(p)) {
//        p->i++;
//        return parser_get(p);
//    } else return tok_none();
//}
//
//bool parser_expect(Parser *p, TokenType expected) { return parser_peek(p).type == expected; }
//
//bool parser_match(Parser *p, TokenType type)
//{
//    if (parser_expect(p, type)) {
//        p->i++;
//        return true;
//    } else return false;
//}
//
//void error_expected_token_type(TokenType expected, Token victim, Token from)
//{
//    if (victim.type == TOK_NONE) {
//        loc_print(from.loc);
//        printf(": ");
//        errorln("expecting `%s`, but got nothing instead.", token_type_as_string(expected));
//    } else {
//        loc_print(victim.loc);
//        printf(": ");
//        errorln("expecting `%s`, but got `%s` instead.", token_type_as_string(expected), token_type_as_string(victim.type));
//    }
//    exit(1);
//}
//
//void error_expected_token_types(TokenType *expected, int n, Token victim, Token from)
//{
//    if (victim.type == TOK_NONE) {
//        loc_print(from.loc);
//        printf(": ");
//        error("expecting ");
//        for (int i = 0; i < n; i ++) {
//            if (i < n-1) printf(", ");
//            else printf(" or ");
//            printf("`%s`", token_type_as_string(expected[i]));
//            printf(", but got nothing instead.\n");
//        }
//    } else {
//        loc_print(victim.loc);
//        printf(": ");
//        error("expecting ");
//        for (int i = 0; i < n; i ++) {
//            if (i < n-1) printf(", ");
//            else printf(" or ");
//            printf("`%s`", token_type_as_string(expected[i]));
//            printf(", but got `%s` instead.\n", token_type_as_string(victim.type));
//        }
//    }
//    exit(1);
//}
//
//void parser_match_type_else_error(Parser *p, TokenType expected, Token from)
//{
//    if (!parser_match(p, expected)) {
//        error_expected_token_type(expected, parser_get(p), from);
//    }
//}
//
//size_t parser_match_types_else_error(Parser *p, TokenType *expected, size_t n, Token from)
//{
//    for (size_t i = 0; i < n; i++) {
//        if (parser_match(p, expected[i])) return i;
//    }
//    error_expected_token_types(expected, n, parser_get(p), from);
//    return 0;
//}
//
//void error_undeclared_variable(Token tok, char *msg)
//{
//    loc_print(tok.loc);
//    printf(": ");
//    error(msg);
//    printf(" undeclared variable `%s`.\n", tok.lexeme);
//    exit(1);
//}
//
//Ops parser_parse(Parser *parser)
//{
//    Ops ops = {0};
//    Token tok;
//    static_assert(__op_types_count == 2, "Cover all op types in parser_parse");
//    do {
//        tok = parser_get(parser);
//        if (tok.type == TOK_NONE) break;
//        switch (tok.type)
//        {
//            case TOK_IDENT:
//            {
//                if (parser_match(parser , TOK_L_PAREN)) {
//                    todo("parse funargs");
//                    // TODO: parse args (every arg is an expr)
//                    if (parser_expect(parser, TOK_INTEGER)) {
//                        //Token t_val = parser_next(parser);
//                        //Op op = {
//                        //    .type = OP_LOAD16,
//                        //    .val_i32 = atoi(t_val.lexeme)
//                        //};
//                        //da_push(&ops, op);
//                    } else if (parser_expect(parser, TOK_IDENT)) {
//                        Token t_var = parser_next(parser);
//                        size_t var_i = global_var_index_by_name(t_var.lexeme);
//                        if (var_i == INDEX_NOT_FOUND) error_undeclared_variable(tok, "");
//                        Op op = {
//                            .type = OP_VAR_READ,
//                            .var_kind = VAR_GLOBAL,
//                            .var_index = var_i
//                        };
//                        da_push(&ops, op);
//                    } else {
//                        TokenType types[2] = {TOK_INTEGER, TOK_IDENT};
//                        error_expected_token_types(types, 2, parser_get(parser), tok);
//                    }
//                    parser_match_type_else_error(parser, TOK_R_PAREN, tok);
//                    parser_match_type_else_error(parser, TOK_SEMICOLON, tok);
//                    //if (streq(tok.lexeme, "print")) {  } // TODO
//                    todo("functions are not yet supported");
//                    exit(1);
//                    break;
//                } else if (parser_expect(parser, TOK_EQUALS)) {
//                    // TODO: look into local variables first then into global variables
//                    size_t var_i = global_var_index_by_name(tok.lexeme);
//                    if (var_i == INDEX_NOT_FOUND) error_undeclared_variable(tok, "trying to assign");
//                    //da_push_many(op, parser_parse_expr(parser)); // TODO
//                    Token t_val = parser_next(parser);
//                    parser_match_type_else_error(parser, TOK_INTEGER, tok);
//                    parser_match_type_else_error(parser, TOK_SEMICOLON, tok);
//
//                    int32_t value = (int32_t)atoi(t_val.lexeme);
//                    Op op = {
//                        .type = OP_VAR_ASSIGN,
//                        .var_kind = VAR_GLOBAL,
//                        .var_index = var_i,
//                        .value.i32 = value
//                    };
//                    da_push(&ops, op);
//                } else {
//                    errorln("unknown word `%s`", tok.lexeme);
//                    exit(1);
//                }
//            } break;  
//            case TOK_LET:
//            {
//                parser_match_type_else_error(parser, TOK_IDENT, tok);
//                Token t_var_name = parser_get(parser);
//                size_t var_i = global_var_index_by_name(t_var_name.lexeme);
//                if (var_i != INDEX_NOT_FOUND) {
//                    Variable var = global_vars.items[var_i]; 
//                    loc_print(tok.loc);
//                    printf(": ");
//                    errorln("Redeclaration of variable `%s`.", var.name);
//                    loc_print(var.loc);
//                    printf(": ");
//                    note(" Declared here the first time.");
//                    exit(1);
//                }
//                var_i = global_vars.count;
//                Variable var = {
//                    .kind = VAR_GLOBAL,
//                    .type = TYPE_I32,
//                    .index = global_vars.count,
//                    .name = strdup(t_var_name.lexeme),
//                    .loc = loc_clone(tok.loc)
//                };
//                if (parser_match(parser, TOK_EQUALS)) {
//                    Token t_val = parser_get(parser);
//                    parser_match_type_else_error(parser, TOK_INTEGER, tok); // TODO: per ora
//                    parser_match_type_else_error(parser, TOK_SEMICOLON, t_val);
//                    var.initialized = true;
//                    var.value.i32 = (int32_t)atoi(t_val.lexeme); // TODO: solo per ora, poi usero' atol per bene
//                } else if (parser_match(parser, TOK_SEMICOLON)) {
//                    // NOTE: nothing to do
//                } else {
//                    TokenType types[] = {TOK_SEMICOLON, TOK_EQUALS};
//                    error_expected_token_types(types, sizeof(types), parser_get(parser), tok);
//                }
//                da_push(&global_vars, var);
//            } break;
//            case TOK_IF:
//            {
//                Token t_condition = parser_get(parser);
//                (void)t_condition;
//                if (!(parser_match(parser, TOK_TRUE) || parser_match(parser, TOK_FALSE))) {
//                    todo("parse if condition");
//                    exit(1);
//                }
//                parser_match_type_else_error(parser, TOK_L_CUPAREN, tok);
//                todo("parse if body"); // TODO (segnare da qualche parte, in uno stack magari, che si e' aperto
//                                       //      un if/blocco e che si si aspetta venga chiuso ad un certo punto)
//                // TODO: pensare all'op per if
//            } break;
//
//            case TOK_NONE: {
//                fprintf(stderr, "Unreachable\n");
//                exit(1);
//            }
//            default: 
//                todo("parse token type %s", token_type_as_string(tok.type));
//                exit(1);
//        }
//    } while (parser_advance(parser));
//    return ops;
//}

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

bool run_cmd(char **cmd)
{
    if (cmd == NULL) return false;
    printf("CMD: ");
    size_t i = 0;
    char *s = cmd[i];
    while (s != NULL) {
        printf("%s ", s);
        s = cmd[++i];
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

static_assert(__astnode_types_count == 13, "Cover all ast node types in generate_c_code");
void generate_c_code(ASTNode *node, FILE *f)
{
    if (!node) return;

    switch (node->type) {
    case ASTNODE_PROGRAM: {
        fprintf(f, "#include <stdint.h>\n\n");

        //da_foreach (global_vars, var)
        //    fprintf(f, "int32_t %s = %d;\n", var->name, 0);
        //fprintf(f, "\n");

        fprintf(f, "int main(void) {\n");
        generate_c_code(node->next, f);
        fprintf(f, "return 0;\n");
        fprintf(f, "}\n");
    } break;

    case ASTNODE_VAR_DECL: {
        fprintf(f, "int32_t %s = ", node->string_value);
        generate_c_code(node->left, f);
        fprintf(f, ";\n");
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_VAR_ASSIGN: {
        fprintf(f, "%s = ", node->string_value);
        generate_c_code(node->left, f);
        fprintf(f, ";\n");
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_BLOCK: {
        fprintf(f, "{\n");
        generate_c_code(node->left, f);
        fprintf(f, "}\n");
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_EXPRESSION_STATEMENT: {
        generate_c_code(node->left, f);
        fprintf(f, ";\n");
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_IF: {
        fprintf(f, "if (");
        generate_c_code(node->left, f);
        fprintf(f, ") ");
        generate_c_code(node->right, f);
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_FN_DECL: {
        fprintf(f, "void %s(void) ", node->string_value);
        generate_c_code(node->left, f);
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_CALL: {
        fprintf(f, "%s(", node->string_value);
        ASTNode *elt = node->left;
        while (elt) {
            generate_c_code(elt, f);
            if (elt->next) fprintf(f, ", ");
            elt = elt->next;
        }
        fprintf(f, ")");
    } break;

    case ASTNODE_BINOP: {
        generate_c_code(node->left, f);
        fprintf(f, " %s ", operator_as_string(node->op));
        generate_c_code(node->right, f);
    } break;

    case ASTNODE_NUMBER: {
        fprintf(f, "%d", node->int_value);
    } break;

    case ASTNODE_STRING: {
        fprintf(f, "\"%s\"", node->string_value);
    } break;

    case ASTNODE_BOOL: {
        fprintf(f, "%d", node->bool_value ? 1 : 0);    
    } break;

    case ASTNODE_IDENT: {
        fprintf(f, "%s", node->string_value);
    } break;

    default:
        printf("Unreachable ast node type %u in generate_c_code\n", node->type);
        exit(1);
    }
}

bool compile_c(void)
{
    char *gcc_cmd[] = {
        "gcc",
        "-o", "kjude_output",
        "__intermediate.c",
        "-Wall", "-Wextra",
        NULL
    };
    return run_cmd(gcc_cmd);
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

    char *source_path = shift_arg(&argc, &argv);
    // TODO: check source_path file extension

    /// Begin Lexing
    time_from_here();

    Lexer lexer = lexer_new(source_path);
    Tokens tokens = lexer_lex(&lexer);
#if true
    printf("Tokens (%zu):\n", tokens.count);
    da_foreach (tokens, t) {
        loc_print(t->loc);
        token_print(*t);
        printf("\n");
    }
    printf("\n");
#endif

    time_to_here();
    time_print("Lexing");
    /// End Lexing

    /// Begin Parsing
    time_from_here();

    Parser parser = parser_new(tokens);
    ASTNode *ast = parse_program(&parser);
    if (ast == NULL) return 1;

    time_to_here();
    time_print("Parsing");
    /// End Parsing

    /// Begin Generation
    time_from_here();

    char *c_filename = "__intermediate.c";
    FILE *c_file = fopen(c_filename, "w");
    if (c_file == NULL) {
        fprintf(stderr, "Could not open file `%s`\n", c_filename);
        exit(1);
    }
    generate_c_code(ast, c_file);
    fclose(c_file);

    time_to_here();
    time_print("Generation");
    /// End Generation

    /// Begin Finalization
    time_from_here();

    compile_c();
    //remove(c_filename);

    time_to_here();
    time_print("Finalization");
    /// End Finalization

    return 0;
}
