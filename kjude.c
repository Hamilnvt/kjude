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
#define DEFAULT_KJUDE_INTERMEDIATE_C_FILE "__kjude_intermediate.c"

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

void unreachable(char *format, ...)
{
    va_list fmt;
    va_start(fmt, format);
    char *string = fmt_to_string(format, fmt);
    va_end(fmt);
    printf("UNREACHABLE: %s", string);
    free(string);
}

void unreachableln(char *format, ...)
{
    va_list fmt;
    va_start(fmt, format);
    char *string = fmt_to_string(format, fmt);
    va_end(fmt);
    printf("UNREACHABLE: %s\n", string);
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
#define KEYWORD_IF     "if"
#define KEYWORD_RETURN "return"
#define KEYWORD_TRUE   "true"
#define KEYWORD_FALSE  "false"

typedef enum
{
    TOK_INVALID = 0,
    TOK_IDENT,
    TOK_IF,
    TOK_RETURN,
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
    TOK_DOUBLE_EQUALS,
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

char *token_type_as_string(TokenType type)
{
    static_assert(__tok_types_count == 23, "Cover all token types in token_type_as_string");
    switch (type)
    {
        case TOK_INVALID:       return "Invalid";
        case TOK_IDENT:         return "Identifier";
        case TOK_IF:            return KEYWORD_IF;
        case TOK_RETURN:        return KEYWORD_RETURN;
        case TOK_TRUE:          return KEYWORD_TRUE;
        case TOK_FALSE:         return KEYWORD_FALSE;
        case TOK_INTEGER:       return "Integer";
        case TOK_STRING:        return "String";
        case TOK_EOF:           return "EOF";
        case TOK_L_PAREN:       return "(";
        case TOK_R_PAREN:       return ")";
        case TOK_L_SQPAREN:     return "[";
        case TOK_R_SQPAREN:     return "]";
        case TOK_L_CUPAREN:     return "{";
        case TOK_R_CUPAREN:     return "}";
        case TOK_COMMA:         return ",";
        case TOK_COLON:         return ":";
        case TOK_SEMICOLON:     return ";";
        case TOK_DOUBLE_EQUALS: return "==";
        case TOK_EQUALS:        return "=";
        case TOK_ARROW:         return "->";
        case TOK_PLUS:          return "+";
        case TOK_STAR:          return "*";
        default:
            errorln("Unknown token type %d in token_type_as_string", type);
            exit(1);
    }
}

static_assert(__tok_types_count == 23, "Cover all token types in token_print");
void token_print(Token tok)
{
    switch (tok.type) {
        case TOK_INVALID:
        case TOK_IDENT:
        case TOK_INTEGER:
        case TOK_STRING:
            printf("<%s, \"%s\">", token_type_as_string(tok.type), tok.lexeme);
            break;

        case TOK_IF:
        case TOK_RETURN:
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
        case TOK_DOUBLE_EQUALS:
        case TOK_ARROW:
        case TOK_PLUS:
        case TOK_STAR:
            printf("\"%s\"", tok.lexeme);
            break;

        default:
            unreachableln("Token type %d in token_print", tok.type);
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

typedef struct ASTNode ASTNode;
typedef struct
{
    Tokens tokens;
    size_t pos;
    ASTNode *current_function;
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

static_assert(__tok_types_count == 23, "Cover all token types in lexer_get_next_token");
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
        
             if (streq(token.lexeme, KEYWORD_IF))     token.type = TOK_IF;
        else if (streq(token.lexeme, KEYWORD_RETURN)) token.type = TOK_RETURN;
        else if (streq(token.lexeme, KEYWORD_TRUE))   token.type = TOK_TRUE;
        else if (streq(token.lexeme, KEYWORD_FALSE))  token.type = TOK_FALSE;
        else                                          token.type = TOK_IDENT;
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
            case '=': {
                if (current_char == '=') {
                    token.type = TOK_DOUBLE_EQUALS;
                    token.lexeme[i++] = current_char;
                    lexer->pos++;
                    lexer->loc.col++;
                } else {
                    token.type = TOK_EQUALS;
                }
            } break;
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
            default: break;
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

typedef enum
{
    ASTNODE_PROGRAM,
    ASTNODE_VAR_DECL,
    ASTNODE_VAR_ASSIGN,
    ASTNODE_BLOCK,
    ASTNODE_IF,
    ASTNODE_RETURN,
    ASTNODE_FN_DECL,
    ASTNODE_CALL,
    ASTNODE_CALL_STATEMENT,
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

    OP_DOUBLE_EQUALS,
    __op_types_count,
} Operator;

static_assert(__op_types_count == 3, "Cover all operators in operator_from_token_type");
Operator operator_from_token_type(TokenType type)
{
    switch (type) {
    case TOK_PLUS:          return OP_PLUS;
    case TOK_STAR:          return OP_STAR;
    case TOK_DOUBLE_EQUALS: return OP_DOUBLE_EQUALS;
    default:
        unreachableln("Token type %u in operator_from_token_type", type);
        exit(1);
    }
}

static_assert(__op_types_count == 3, "Cover all operators in operator_as_string");
char *operator_as_string(Operator op)
{
    switch (op) {
    case OP_PLUS:          return "+";
    case OP_STAR:          return "*";
    case OP_DOUBLE_EQUALS: return "==";
    default:
        unreachableln("Operator %u in operator_as_string", op);
        exit(1);
    }
}

typedef enum
{
    TYPE_INVALID = 0,
    TYPE_NOT_DECLARED,
    TYPE_NOT_INFERRED,

    TYPE_VOID,
    TYPE_I32,
    TYPE_STRING,
    TYPE_BOOL,

    __types_count
} KjudeType;

static_assert(__types_count == 7, "Cover all types in type_as_string");
char *type_as_string(KjudeType type)
{
    switch (type) {
    case TYPE_INVALID:      return "Invalid";
    case TYPE_NOT_DECLARED: return "Not declared";
    case TYPE_NOT_INFERRED: return "Not inferred";

    case TYPE_VOID:   return "void";
    case TYPE_I32:    return "int";
    case TYPE_STRING: return "string";
    case TYPE_BOOL:   return "bool";
    default:
        unreachableln("Type %u in type_as_string", type);
        exit(1);
    }
}

static_assert(__types_count == 7, "Cover all types in kjude_to_c_types_conversion_table");
static char *kjude_to_c_types_conversion_table[__types_count] = {
    [TYPE_INVALID]      = NULL,
    [TYPE_NOT_DECLARED] = NULL,
    [TYPE_NOT_INFERRED] = NULL,
    [TYPE_VOID]         = "void",
    [TYPE_I32]          = "int32_t",
    [TYPE_STRING]       = "char *",
    [TYPE_BOOL]         = "int",
};

char *c_type_from_kjude_type(KjudeType type)
{
    if (type >= __types_count) return NULL;
    return kjude_to_c_types_conversion_table[type];
}

static_assert(__types_count == 7, "Cover all types in KjudeValue union");
typedef union
{
    char string[64];
    bool _bool;
    int32_t _int;
} KjudeValue;

typedef struct
{
    Location loc;
    char name[64];
    KjudeType type;
    char type_string[64];
} Variable;

typedef struct
{
    Variable **items;
    size_t count;
    size_t capacity;
} Variables;

typedef struct
{
    Location loc;
    char name[64];
    Variables params;
    KjudeType ret_type;
    char ret_type_string[64];
} Function;

typedef struct
{
    Function **items;
    size_t count;
    size_t capacity;
} Functions;

typedef struct
{
    Functions functions;
    Variables variables;
} Scope;

typedef struct
{
    Scope *items;
    size_t count;
    size_t capacity;
} Scopes;
static Scopes scopes = {0};

Variable *get_variable(char *name)
{
    int i = scopes.count-1;
    while (i >= 0) {
        Variables *variables = &scopes.items[i].variables;
        for (size_t j = 0; j < variables->count; j++) {
            Variable *var = variables->items[j];
            if (streq(name, var->name)) return var;
        }
        i--;
    }
    return NULL;
}

Function *get_function(char *name)
{
    int i = scopes.count-1;
    while (i >= 0) {
        Functions *functions = &scopes.items[i].functions;
        for (size_t j = 0; j < functions->count; j++) {
            Function *fn = functions->items[j];
            if (streq(name, fn->name)) return fn;
        }
        i--;
    }
    return NULL;
}

typedef struct ASTNode
{
    ASTNodeType type;
    Location loc;

    KjudeType inferred_expr_type;

    union {
        KjudeValue value;
        Operator op;

        struct {
            char name[64];
            Variable *sym;
        } var;
        struct {
            char name[64];
            Function *sym;
        } fn;
    };
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *next;
} ASTNode;

ASTNode *create_node(ASTNodeType type, Location loc)
{
    ASTNode *node = malloc(sizeof(ASTNode));
    *node = (ASTNode){
        .type = type,
        .loc = loc,
        .inferred_expr_type = TYPE_NOT_INFERRED
    };
    return node;
}

typedef ASTNode *(*PrefixExprFn)(Parser *);
typedef ASTNode *(*InfixExprFn)(Parser *, ASTNode *left);

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

Token *parser_peek_n(Parser *p, size_t n)
{
    if (p->pos + n >= p->tokens.count) return NULL;
    else return &p->tokens.items[p->pos + n];
}

static inline Token *parser_peek(Parser *p) { return parser_peek_n(p, 1); }

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

static inline ExprRule* get_expression_rule(TokenType type)
{
    extern ExprRule expression_rules[];
    assert(type < __tok_types_count);
    return &expression_rules[type];
}

ASTNode *parse_number(Parser *parser)
{
    ASTNode *node = create_node(ASTNODE_NUMBER, current_token->loc);
    node->value._int = atoi(current_token->lexeme);
    parser_advance(parser);
    return node;
}

ASTNode *parse_string(Parser *parser)
{
    ASTNode *node = create_node(ASTNODE_STRING, current_token->loc);
    strcpy(node->value.string, current_token->lexeme);
    parser_advance(parser);
    return node;
}

ASTNode *parse_expression_with_precedence(Parser *parser, Precedence precedence)
{
    PrefixExprFn prefix_rule = get_expression_rule(current_token->type)->prefix;
    if (!prefix_rule) return NULL;

    ASTNode *expr = prefix_rule(parser);
    
    while (precedence < get_expression_rule(current_token->type)->precedence) {
        InfixExprFn infix_rule = get_expression_rule(current_token->type)->infix;
        expr = infix_rule(parser, expr);
    }
    
    return expr;
}

static inline ASTNode *parse_expression(Parser *parser) { return parse_expression_with_precedence(parser, PREC_NONE); }

ASTNode *parse_grouping(Parser *parser)
{
    parser_advance(parser); // "("
    ASTNode *node = parse_expression(parser);
    if (!node) {
        loc_print(current_token->loc);
        error("Expected expression, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }
    parser_expect(parser, TOK_R_PAREN);
    parser_advance(parser);
    return node;
}

//static_assert(__types_count == 1, "Cover all types in size_of_type");
//size_t size_of_type(KjudeType type)
//{
//    switch (type)
//    {
//        case TYPE_I32: return sizeof(int32_t);
//        default: 
//            errorln("Unreachable type %u in size_of_type", type);
//            exit(1);
//    }
//}

static_assert(__types_count == 7, "Cover all types in get_type_from_string");
KjudeType get_type_from_string(char *type_string)
{
         if (streq(type_string, type_as_string(TYPE_VOID)))   return TYPE_VOID;
    else if (streq(type_string, type_as_string(TYPE_I32)))    return TYPE_I32;
    else if (streq(type_string, type_as_string(TYPE_STRING))) return TYPE_STRING;
    else if (streq(type_string, type_as_string(TYPE_BOOL)))   return TYPE_BOOL;
    else                                                      return TYPE_NOT_DECLARED;
}

KjudeType parse_type(Parser *parser, char not_declared_string[])
{
    if (current_token->type != TOK_IDENT) {
        loc_print(current_token->loc);
        error("Expecting type but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }

    KjudeType type = TYPE_INVALID;

    if (current_token->type == TOK_IDENT) {
        type = get_type_from_string(current_token->lexeme); 
        if (not_declared_string && type == TYPE_NOT_DECLARED) {
            strcpy(not_declared_string, current_token->lexeme);
        }
        parser_advance(parser); // type
    } else {
        loc_print(current_token->loc);
        error("Expecting a type, but got token ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }

    return type;
}

ASTNode *parse_var_decl(Parser *parser)
{
    ASTNode *node = create_node(ASTNODE_VAR_DECL, current_token->loc);

    node->var.sym = malloc(sizeof(Variable));
    if (!node->var.sym) {} // TODO

    if (current_token->type != TOK_IDENT) {
        loc_print(current_token->loc);
        error("Expected variable name, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }

    node->var.sym->loc = current_token->loc;
    strcpy(node->var.sym->name, current_token->lexeme);
    strcpy(node->var.name, current_token->lexeme);
    parser_advance(parser); // var name
    parser_expect(parser, TOK_COLON);

    if (current_token->type == TOK_EQUALS) {
        node->var.sym->type = TYPE_NOT_INFERRED;
    } else {
        node->var.sym->type = parse_type(parser, node->var.sym->type_string);
    }

    if (current_token->type != TOK_SEMICOLON) {
        parser_expect(parser, TOK_EQUALS);
        node->left = parse_expression(parser);
        if (!node->left) {
            loc_print(current_token->loc);
            error("Expected expression, but got ");
            token_print(*current_token);
            printf("\n");
            exit(1);
        }
    }

    parser_expect(parser, TOK_SEMICOLON);

    return node;
}

ASTNode *parse_statement(Parser *parser);

ASTNode *parse_block(Parser *parser)
{
    parser_advance(parser); // '{'

    ASTNode *root = create_node(ASTNODE_BLOCK, current_token->loc);

    if (!current_token) {
        printf("ERROR: TODO %u\n", __LINE__);
        exit(1);
    }
    
    if (current_token->type == TOK_R_CUPAREN) { // empty block
        parser_advance(parser); // '}'
        return root;
    }

    root->left = parse_statement(parser); 
    if (root->left) {
        ASTNode *current_node = root->left;
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

Variable *parse_parameter(Parser *parser)
{
    Variable *var = malloc(sizeof(Variable));
    if (!var) { } // TODO

    if (current_token->type != TOK_IDENT) {
        loc_print(current_token->loc);
        error("Expected parameter name, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }

    var->loc = current_token->loc;
    strcpy(var->name, current_token->lexeme);
    parser_advance(parser); // var name
    parser_expect(parser, TOK_COLON);

    var->type = parse_type(parser, var->type_string);

    return var;
}

ASTNode *parse_fn_decl(Parser *parser)
{
    ASTNode *node = create_node(ASTNODE_FN_DECL, current_token->loc);
    parser->current_function = node;

    node->fn.sym = malloc(sizeof(Function));
    if (!node->fn.sym) { } // TODO

    if (current_token->type != TOK_IDENT) {
        loc_print(current_token->loc);
        error("Expected function name, but got ");
        token_print(*current_token);
        printf("\n");
        exit(1);
    }

    node->fn.sym->loc = current_token->loc;
    strcpy(node->fn.sym->name, current_token->lexeme);
    strcpy(node->fn.name, current_token->lexeme);
    parser_advance(parser); // function name

    parser_expect(parser, TOK_COLON);

    parser_expect(parser, TOK_L_PAREN);

    if (current_token->type != TOK_R_PAREN) {
        Variables params = {0};
        do {
            Variable *param = parse_parameter(parser);
            da_push(&params, param);
            if (current_token->type == TOK_R_PAREN) break;
            else if (current_token->type == TOK_COMMA)
                parser_advance(parser); // ","
        } while (true);
        node->fn.sym->params = params;
    }
    parser_expect(parser, TOK_R_PAREN);

    if (current_token->type == TOK_ARROW) {
        parser_advance(parser); // "->"
        node->fn.sym->ret_type = parse_type(parser, node->fn.sym->ret_type_string);
    } else {
        node->fn.sym->ret_type = TYPE_VOID;
    }

    parser_expect(parser, TOK_EQUALS);

    // TODO: maybe if current != { we can parse a single statement, like in if but, at least here we got = to divide
    node->left = parse_block(parser);

    parser->current_function = NULL;
    return node;
}

ASTNode *parse_true(Parser *parser)
{
    ASTNode *node = create_node(ASTNODE_BOOL, current_token->loc);
    node->value._bool = true;
    parser_advance(parser);
    return node;
}
ASTNode *parse_false(Parser *parser)
{
    ASTNode *node = create_node(ASTNODE_BOOL, current_token->loc);
    node->value._bool = false;
    parser_advance(parser);
    return node;
}

ASTNode *parse_if(Parser *parser)
{
    parser_advance(parser); // "if"

    ASTNode *node = create_node(ASTNODE_IF, current_token->loc);
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
        ASTNode *block = create_node(ASTNODE_BLOCK, current_token->loc);
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

ASTNode *parse_return(Parser *parser)
{
    if (!parser->current_function) {
        loc_print(current_token->loc);
        errorln("Unexpected return outside of function");
        exit(1);
    }

    ASTNode *node = create_node(ASTNODE_RETURN, current_token->loc);
    node->left = parser->current_function;

    parser_advance(parser); // "return"

    if (current_token->type != TOK_SEMICOLON) {
        node->right = parse_expression(parser);
    }
    parser_expect(parser, TOK_SEMICOLON);

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
    ASTNode *node = create_node(ASTNODE_CALL, current_token->loc);

    strcpy(node->fn.name, current_token->lexeme);
    parser_advance(parser); // function name

    parser_advance(parser); // '('
    node->left = parse_expression_list(parser);
    parser_expect(parser, TOK_R_PAREN);

    return node;
}

ASTNode *parse_ident(Parser *parser)
{
    Token *next = parser_peek(parser); 
    if (next && next->type == TOK_L_PAREN) return parse_call(parser);

    ASTNode *node = create_node(ASTNODE_IDENT, current_token->loc);

    strcpy(node->var.name, current_token->lexeme);
    parser_advance(parser);
    return node;
}

ASTNode *parse_var_assignment(Parser *parser)
{
    ASTNode *node = create_node(ASTNODE_VAR_ASSIGN, current_token->loc);
    strcpy(node->var.name, current_token->lexeme);
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
    ASTNode *node = create_node(ASTNODE_BINOP, current_token->loc);
    node->op = operator_from_token_type(op_token->type);
    parser_advance(parser);
    ExprRule *rule = get_expression_rule(op_token->type);
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

static_assert(__tok_types_count == 23, "Cover all token types in expression_rules table");
ExprRule expression_rules[__tok_types_count] = {
    // No expression rules related to these tokens
    [TOK_INVALID]    = {NULL, NULL, PREC_NONE},
    [TOK_IF]         = {NULL, NULL, PREC_NONE},
    [TOK_RETURN]     = {NULL, NULL, PREC_NONE},
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

    // Primaries
    [TOK_IDENT]   = {parse_ident,  NULL, PREC_NONE},
    [TOK_INTEGER] = {parse_number, NULL, PREC_NONE},
    [TOK_STRING]  = {parse_string, NULL, PREC_NONE},
    [TOK_TRUE]    = {parse_true,   NULL, PREC_NONE},
    [TOK_FALSE]   = {parse_false,  NULL, PREC_NONE},

    // Grouping
    [TOK_L_PAREN] = {parse_grouping, NULL, PREC_NONE},
    [TOK_R_PAREN] = {NULL,           NULL, PREC_NONE},

    // Arithmetic operators
    [TOK_PLUS]    = {NULL, parse_binary, PREC_TERM},
    [TOK_STAR]    = {NULL, parse_binary, PREC_FACTOR},

    // Logical operators
    [TOK_DOUBLE_EQUALS] = {NULL, parse_binary, PREC_TERM},
};

ASTNode *parse_call_statement(Parser *parser)
{
    ASTNode *node = create_node(ASTNODE_CALL_STATEMENT, current_token->loc);

    strcpy(node->fn.name, current_token->lexeme);
    parser_advance(parser); // function name

    parser_advance(parser); // '('
    node->left = parse_expression_list(parser);
    parser_expect(parser, TOK_R_PAREN);

    parser_expect(parser, TOK_SEMICOLON);
    return node;
}

static_assert(__astnode_types_count == 14, "Cover all ast node types in parsing");
ASTNode *parse_statement(Parser *parser)
{
    ASTNode *node = NULL;
    Token *token = current_token;
    switch (token->type) {
        case TOK_IF: {
            node = parse_if(parser);
        } break;

        case TOK_RETURN: {
            node = parse_return(parser);
        } break;

        case TOK_IDENT: {
            Token *next = parser_peek(parser);
            if (next && next->type == TOK_EQUALS) {
                node = parse_var_assignment(parser);
            } else if (next && next->type == TOK_COLON) {
                next = parser_peek_n(parser, 2);
                if (next->type == TOK_L_PAREN) {
                    node = parse_fn_decl(parser);
                } else {
                    node = parse_var_decl(parser);
                }
            } else if (next && next->type == TOK_L_PAREN) {
                node = parse_call_statement(parser);
            } else {
                loc_print(token->loc);
                error("Expecting a statement, but got ");
                token_print(*token);
                printf("\n");
                exit(1);
            }
        } break;

        case TOK_INVALID: {
            loc_print(token->loc);
            errorln("Invalid token \"%s\" starting a statement", token->lexeme);
            exit(1);
        }

        default:
            loc_print(token->loc);
            error("Unexpected token ");
            token_print(*token);
            printf(" starting a statement\n");
            loc_print(token->loc);
            note("A statement is a declaration, an assignment or a function call");
            exit(1);
    }

    return node;
}

ASTNode *parse_program(Parser *parser)
{
    ASTNode *ast = create_node(ASTNODE_PROGRAM, current_token->loc);
    ASTNode *node = ast;

    Scope global_scope = {0};
    da_push(&scopes, global_scope);

    while (current_token->type != TOK_EOF) {
        node->next = parse_statement(parser);
        if (node->next) node = node->next;
    }
    
    return ast;
}

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

static inline void generate_fn_signature(Function *fn, FILE *f)
{
    fprintf(f, "%s %s(", c_type_from_kjude_type(fn->ret_type), fn->name);
    if (da_is_empty(&fn->params)) {
        fprintf(f, "void");
    } else {
        for (size_t i = 0; i < fn->params.count; i++) {
            Variable *param = fn->params.items[i];
            fprintf(f, "%s %s", c_type_from_kjude_type(param->type), param->name);
            if (i < fn->params.count-1) fprintf(f, ", ");
        }
    }
    fprintf(f, ")");
}

static_assert(__astnode_types_count == 14, "Cover all ast node types in generate_c_code");
void generate_c_code(ASTNode *node, FILE *f)
{
    if (!node) return;

    switch (node->type) {
    case ASTNODE_PROGRAM: {
        fprintf(f, "#include <stdint.h>\n\n");

        Functions global_functions = scopes.items[0].functions;
        for (size_t i = 0; i < global_functions.count; i++) {
            Function *fn = global_functions.items[i];
            generate_fn_signature(fn, f);
            fprintf(f, ";\n");
        }
        fprintf(f, "\n");

        // TODO: what if they were defined at declaration? (v := 5)
        // - maybe I should add a compile time value to the variable struct, but this is too big now
        // - so for now I will just forbid them
        Variables global_variables = scopes.items[0].variables;
        if (!da_is_empty(&global_variables)) {
            errorln("Global variables are not allowed at the moment");
            for (size_t i = 0; i < global_variables.count; i++) {
                Variable *var = global_variables.items[i];
                loc_print(var->loc);
                note("\"%s\": %s\n", var->name, type_as_string(var->type));
            }
            printf("\n");
            exit(1);
        }

        generate_c_code(node->next, f);
    } break;

    case ASTNODE_VAR_DECL: {
        fprintf(f, "%s %s", c_type_from_kjude_type(node->var.sym->type), node->var.sym->name);
        if (node->left) {
            fprintf(f, " = ");
            generate_c_code(node->left, f);
        }
        fprintf(f, ";\n");
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_VAR_ASSIGN: {
        fprintf(f, "%s = ", node->var.name);
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

    case ASTNODE_CALL_STATEMENT: {
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

    case ASTNODE_RETURN: {
        fprintf(f, "return");
        if (node->right) {
            fprintf(f, " ");
            generate_c_code(node->right, f);
        }
        fprintf(f, ";\n");
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_FN_DECL: {
        if (streq(node->fn.name, "main")) {
            if (node->fn.sym->ret_type != TYPE_I32) {
                loc_print(node->loc);
                errorln("main function must return %s, but got %s", type_as_string(TYPE_I32),
                        type_as_string(node->fn.sym->ret_type));
                exit(1);
            }
            fprintf(f, "int main(int argc, char **argv) ");
        } else {
            generate_fn_signature(node->fn.sym, f);
            fprintf(f, " ");
        }
        generate_c_code(node->left, f);
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_CALL: {
        fprintf(f, "%s(", node->fn.name);
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
        fprintf(f, "%d", node->value._int);
    } break;

    case ASTNODE_STRING: {
        fprintf(f, "\"%s\"", node->value.string);
    } break;

    case ASTNODE_BOOL: {
        fprintf(f, "%d", node->value._bool ? 1 : 0);    
    } break;

    case ASTNODE_IDENT: {
        fprintf(f, "%s", node->var.name);
    } break;

    default:
        unreachableln("Ast node type %u in generate_c_code\n", node->type);
        exit(1);
    }
}

bool compile_c(void)
{
    char *gcc_cmd[] = {
        "gcc",
        "-o", "kjude_output",
        DEFAULT_KJUDE_INTERMEDIATE_C_FILE,
        "-Wall", "-Wextra",
        NULL
    };
    return run_cmd(gcc_cmd);
}

KjudeType binop_type(ASTNode *node);

static_assert(__astnode_types_count == 14, "Cover all ast node types in node_type");
KjudeType node_type(ASTNode *node)
{
    if (!node) {
        unreachableln("NULL node in node_type");
        return TYPE_INVALID;
    }

    if (node->inferred_expr_type == TYPE_INVALID)      return TYPE_INVALID;
    if (node->inferred_expr_type != TYPE_NOT_INFERRED) return node->inferred_expr_type;

    KjudeType result_type = TYPE_INVALID;

    switch (node->type) {

    case ASTNODE_PROGRAM:
    case ASTNODE_VAR_DECL:
    case ASTNODE_VAR_ASSIGN:
    case ASTNODE_BLOCK:
    case ASTNODE_IF:
    case ASTNODE_RETURN:
    case ASTNODE_FN_DECL:
    {
        unreachableln("Node %u in node_type", node->type);
        exit(1);
    }

    case ASTNODE_CALL_STATEMENT: result_type = node_type(node->next); break;

    case ASTNODE_CALL: {
        printf("ASTNODE_CALL node %p\n", node);
        printf("  %s -> %p\n", node->fn.name, node->fn.sym);
        if (!node->fn.sym) {
            Function *fn = get_function(node->fn.name);
            if (!fn) {
                result_type = TYPE_NOT_DECLARED;
                break;
            } 
            node->fn.sym = fn;
        }
        result_type = node->fn.sym->ret_type;
    } break;

    case ASTNODE_BINOP: result_type = binop_type(node); break;

    case ASTNODE_NUMBER: result_type = TYPE_I32;    break;
    case ASTNODE_STRING: result_type = TYPE_STRING; break;
    case ASTNODE_BOOL:   result_type = TYPE_BOOL;   break;

    case ASTNODE_IDENT: {
        Variable *var = get_variable(node->var.name);
        if (var) {
            result_type = var->type;
            break;
        }
        Function *fn = get_function(node->var.name);
        if (fn) result_type = fn->ret_type;
    } break;

    default:
        unreachableln("Ast node type %u in node_type\n", node->type);
        exit(1);
    }

    node->inferred_expr_type = result_type;
    return result_type;
}

// TODO: here I can do shortcircuit optimizations
KjudeType binop_type(ASTNode *node)
{
    KjudeType left  = node_type(node->left);
    KjudeType right = node_type(node->right);

    switch (node->op) {
    case OP_PLUS:
        if (left == right) return left;
        break;
    case OP_STAR:
        printf("Multiplication between %s and %s\n", type_as_string(left), type_as_string(right));
        if (left == right) return left;
        break;

    case OP_DOUBLE_EQUALS: return TYPE_BOOL;

    default:
        unreachableln("Operator %u in binop_type", node->op);
        exit(1);
    }

    return TYPE_INVALID;
}

void match_type(ASTNode *node, KjudeType type)
{
    KjudeType n_type = node_type(node);
    if (n_type != type) {
        loc_print(node->loc);
        errorln("Expected type \"%s\", but got \"%s\"", type_as_string(type), type_as_string(n_type));
        exit(1);
    }
}

void register_global_functions(ASTNode *root)
{
    ASTNode *current = root;
    while (current) {
        if (current->type == ASTNODE_BLOCK) {
            current = current->next;
            continue;
        }
        if (current->type == ASTNODE_FN_DECL) {
            printf("Register global function '%s' -> %p\n", current->fn.name, current->fn.sym);
            Function *fn = get_function(current->fn.name);
            if (fn) {
                loc_print(current->loc);
                errorln("Redeclaration of function \"%s\"", current->fn.name);
                loc_print(fn->loc);
                note("First declared here");
                exit(1);
            }
            da_push(&scopes.items[0].functions, current->fn.sym);
        }
        current = current->next;
    }
}

static_assert(__astnode_types_count == 14, "Cover all ast node types in type_check");
void type_check(ASTNode *node)
{
    if (!node) return;

    switch (node->type) {
    case ASTNODE_PROGRAM: {
        register_global_functions(node);
        type_check(node->next);
    } break;

    case ASTNODE_VAR_DECL: {
        Variable *var = get_variable(node->var.name);
        if (var) {
            loc_print(node->loc);
            errorln("Redeclaration of variable \"%s\"", node->var.name);
            loc_print(var->loc);
            note("First declared here");
            exit(1);
        }

        if (node->var.sym->type == TYPE_NOT_INFERRED) {
           node->var.sym->type = node_type(node->left);
        }
        if (node->var.sym->type == TYPE_NOT_DECLARED) {
            loc_print(node->loc);
            todo("search for type \"%s\" (var \"%s\") in ASTNODE_VAR_DECL", node->var.sym->type_string,
                    node->var.sym->name);
            exit(1);

            if (node->var.sym->type == TYPE_NOT_DECLARED) {
                loc_print(node->loc);
                errorln("Undeclared type \"%s\" for variable \"%s\"", node->var.sym->type_string,
                        node->var.sym->name);
                exit(1);
            }
        }
        if (node->var.sym->type == TYPE_INVALID) {
            loc_print(node->loc);
            errorln("Invalid type for variable \"%s\"", node->var.sym->name);
            exit(1);
        }
        match_type(node->left, node->var.sym->type);
        da_push(&da_get_last(scopes)->variables, node->var.sym);
        type_check(node->next);
    } break;

    case ASTNODE_VAR_ASSIGN: {
        Variable *var = get_variable(node->var.name);
        if (!var) {
            loc_print(node->loc);
            errorln("Variable \"%s\" is not declared", node->var.name);
            exit(1);
        }
        node->var.sym = var;
        match_type(node->left, var->type);
        type_check(node->next);
    } break;

    case ASTNODE_BLOCK: {
        da_push(&scopes, (Scope){0}); {
            type_check(node->left);
        } da_pop(&scopes);
        type_check(node->next);
    } break;

    case ASTNODE_CALL_STATEMENT: {
        Function *fn = get_function(node->fn.name);
        if (!fn) {
            loc_print(node->loc);
            errorln("Function \"%s\" is not declared", node->fn.name);
            exit(1);
        }
        node->fn.sym = fn;

        type_check(node->left);
        type_check(node->next);
    } break;

    case ASTNODE_IF: {
        match_type(node->left, TYPE_BOOL);
        type_check(node->right);
        type_check(node->next);
    } break;

    case ASTNODE_RETURN: {
        match_type(node->right, node->left->fn.sym->ret_type);
        type_check(node->next);
    } break;

    case ASTNODE_FN_DECL: {
        Function *fn = get_function(node->fn.name);
        if (fn) {
            loc_print(node->loc);
            errorln("Redeclaration of function \"%s\"", node->fn.name);
            loc_print(fn->loc);
            note("First declared here");
            exit(1);
        }

        // TODO: check all parameters types

        if (node->fn.sym->ret_type == TYPE_NOT_DECLARED) { // TODO: invalid?
            loc_print(node->loc);
            todo("search for return type \"%s\" (function \"%s\") in ASTNODE_FN_DECL", node->fn.sym->ret_type_string,
                    node->fn.sym->name);
            exit(1);

            if (node->fn.sym->ret_type == TYPE_NOT_DECLARED) {
                loc_print(node->loc);
                errorln("Undeclared return type \"%s\" for function \"%s\"", node->fn.sym->ret_type_string, node->fn.sym->name);
                exit(1);
            }
        }

        da_push(&da_get_last(scopes)->functions, node->fn.sym);

        Scope fn_scope = {0};
        for (size_t i = 0; i < node->fn.sym->params.count; i++) {
            da_push(&fn_scope.variables, node->fn.sym->params.items[i]);
        }
        da_push(&scopes, fn_scope); {
            type_check(node->left);
        } da_pop(&scopes);

        type_check(node->next);
    } break;

    case ASTNODE_CALL: {
        Function *fn = get_function(node->fn.name);
        if (!fn) {
            loc_print(node->loc);
            errorln("Function \"%s\" is not declared", node->fn.name);
            exit(1);
        }
        node->fn.sym = fn;

        loc_print(node->loc);
        todo("type check call arguments matching function parameters for \"%s\"", node->fn.name);
    } break;

    case ASTNODE_BINOP: {
        type_check(node->left);
        type_check(node->right);
    } break;

    case ASTNODE_IDENT: {
        Variable *var = get_variable(node->var.name);
        if (!var) {
            loc_print(node->loc);
            errorln("Variable \"%s\" is not declared", node->var.name);
            exit(1);
        }
        node->var.sym = var;
    } break;

    case ASTNODE_NUMBER:
    case ASTNODE_STRING:
    case ASTNODE_BOOL:
        break;

    default:
        unreachableln("Ast node type %u in type_check\n", node->type);
        exit(1);
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

    char *source_path = shift_arg(&argc, &argv);
    // TODO: check source_path file extension

    /// Begin Lexing
    time_from_here();

    Lexer lexer = lexer_new(source_path);
    Tokens tokens = lexer_lex(&lexer);
#if DEBUG
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

    /// Begin Type checking
    time_from_here();

    type_check(ast);

    time_to_here();
    time_print("Type checking");
    /// End Type checking

    /// Begin Generation
    time_from_here();

    char *c_filename = DEFAULT_KJUDE_INTERMEDIATE_C_FILE;
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
