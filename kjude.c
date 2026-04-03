// TODO:
// - write grammar
// - invalidate C names for functions and variables (or better, mangle!)
// - aliases and types
//   > an alias is just an alias to a type, useful when you don't want to write 1000 times [string] so you just:
//     `strings : alias = [string]`
//   > a type definition is a distinct copy of an existing type, e.g.:
//     `Index : type = int`
//   > NOTE: inference over something like this will be resolved to an alias (or to a type? I'm not sure):
//     `strings := [strings]`
// - when looking for not declaration or redeclaration, search in all symbols
// - I really want to save are_types_equal_with_errors errors somewhere to print them after some time
// - Indent generated code
// - ASTNODE_RETURN is problematic, I may need to keep a stack of current_functions
// - forward declaration of main has signature `int32_t main(void);` which is incorrect
//   > I may have to introduce C Types, but it does not complain
// - `x++` or `x--` are just shorhands for `x += 1` and `x -= 1`
// - some assignments are not possible:
//   > symbols declared with const
//   > structs and functions definitions (must declare a 'bare' variable to hold it)
// - initialize global variables
// - default values for struct fields and function parameters
// - buffered output
// - type checking function I must ensure that there is a return and that it's of the correct type
// - to avoid naming conflicts in the symbols (like same function in two differenct structs), I can name the function Struct.function (or add a prefix member to Symbol since they can chain Struct1.Struct2.function, where Struct1.Struct2 is the prefix)
// - functions in struct can be:
//   > variables holding functions (function pointers), defined as fptr: (int) -> bool
//   > struct functions:
//     - static: normal functions -> can be called as Struct.f() or s.f() if s is a Struct
//     - method: first argument is self (automatica type Struct) -> can be called as s.f() if s is a Struct and s is passed as first argument
// - structs should be generated like this:
//   ```
//   typedef struct S S;
//   void print(S s); // so the functions can use them
//   struct S {
//       int32_t x;
//   };
//   ```
//   > order is structs decl -> vars decls(+init) -> fns decls -> structs defs -> fns defs
// - should lists be special structs? Otherwise how would I call methods on them?
//   > the functions can just be `list_add(L, e);`, but i'd like to try to make it `L.add(e);`
//     - which suspiciously resembles a struct method call
//   > or there are some functions that can be called on types
//     - `add : (list: List) -> { list.data ... }`
//        > so yeah, they are a struct { data, size, capacity } (capacity if the list is dynamic)
// - Introduce new kinds:
//   > pointer
//   > type

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
#define KJUDE_FILE_EXTENSION "kj"

static inline bool streq(const char *s1, const char *s2) { return strcmp((s1), (s2)) == 0; }

/// Begin Logging

#define ANSI_RESET   "\x1b[0m"
#define ANSI_GREEN   "\x1b[38;2;80;200;120m"  /* info        */
#define ANSI_YELLOW  "\x1b[38;2;230;200;80m"  /* note        */
#define ANSI_ORANGE  "\x1b[38;2;220;130;40m"  /* warning     */
#define ANSI_RED     "\x1b[38;2;210;70;70m"   /* error       */
#define ANSI_MAGENTA "\x1b[38;2;180;80;220m"  /* unreachable */
#define ANSI_BLUE    "\x1b[38;2;80;150;220m"  /* todo        */
#define ANSI_GRAY    "\x1b[38;2;130;130;130m" /* debug       */
 
typedef enum
{
    LOG_INFO,
    LOG_NOTE,
    LOG_WARNING,
    LOG_ERROR,
    LOG_UNREACHABLE,
    LOG_TODO,
    LOG_DEBUG,
} LogLevel;
 
static inline const char *log_color(LogLevel level)
{
    switch (level) {
        case LOG_INFO:        return ANSI_GREEN;
        case LOG_NOTE:        return ANSI_YELLOW;
        case LOG_WARNING:     return ANSI_ORANGE;
        case LOG_ERROR:       return ANSI_RED;
        case LOG_UNREACHABLE: return ANSI_MAGENTA;
        case LOG_TODO:        return ANSI_BLUE;
        case LOG_DEBUG:       return ANSI_GRAY;
        default:              return ANSI_RESET;
    }
}
 
static inline const char *log_label(LogLevel level)
{
    switch (level) {
        case LOG_INFO:        return "INFO";
        case LOG_NOTE:        return "NOTE";
        case LOG_WARNING:     return "WARNING";
        case LOG_ERROR:       return "ERROR";
        case LOG_UNREACHABLE: return "UNREACHABLE";
        case LOG_TODO:        return "TODO";
        case LOG_DEBUG:       return "DEBUG";
        default:              return "LOG";
    }
}

#if defined(__GNUC__) || defined(__clang__)
#  define LOG_PRINTF_LIKE(fmt_idx, args_idx) \
    __attribute__((format(printf, fmt_idx, args_idx)))
#else
#  define LOG_PRINTF_LIKE(fmt_idx, args_idx)
#endif

static inline char *fmt_to_string(const char *format, va_list fmt)
{
    va_list fmt_copy;
    va_copy(fmt_copy, fmt);
    size_t len = vsnprintf(NULL, 0, format, fmt_copy) + 1;
    va_end(fmt_copy);
    char *string = malloc(sizeof(char) * len);
    if (!string) return NULL;
    vsnprintf(string, len, format, fmt);
    return string;
}
 
static inline void log_write(LogLevel level, int newline, const char *format, va_list fmt)
{
    char *string = fmt_to_string(format, fmt);
    if (!string) return;
    printf("%s%s" ANSI_RESET ": %s%s",
           log_color(level), log_label(level),
           string,
           newline ? "\n" : "");
    free(string);
}
 
static inline void LOG_PRINTF_LIKE(2, 3) log_msg(LogLevel level, const char *format, ...)
{
    va_list fmt;
    va_start(fmt, format);
    log_write(level, 0, format, fmt);
    va_end(fmt);
}
 
static inline void LOG_PRINTF_LIKE(2, 3) log_msgln(LogLevel level, const char *format, ...)
{
    va_list fmt;
    va_start(fmt, format);
    log_write(level, 1, format, fmt);
    va_end(fmt);
}
 
#define info(fmt, ...)          log_msg   (LOG_INFO,        fmt, ##__VA_ARGS__)
#define infoln(fmt, ...)        log_msgln (LOG_INFO,        fmt, ##__VA_ARGS__)
#define note(fmt, ...)          log_msg   (LOG_NOTE,        fmt, ##__VA_ARGS__)
#define noteln(fmt, ...)        log_msgln (LOG_NOTE,        fmt, ##__VA_ARGS__)
#define warning(fmt, ...)       log_msg   (LOG_WARNING,     fmt, ##__VA_ARGS__)
#define warningln(fmt, ...)     log_msgln (LOG_WARNING,     fmt, ##__VA_ARGS__)
#define error(fmt, ...)         log_msg   (LOG_ERROR,       fmt, ##__VA_ARGS__)
#define errorln(fmt, ...)       log_msgln (LOG_ERROR,       fmt, ##__VA_ARGS__)
#define unreachable(fmt, ...)   log_msg   (LOG_UNREACHABLE, fmt, ##__VA_ARGS__)
#define unreachableln(fmt, ...) log_msgln (LOG_UNREACHABLE, fmt, ##__VA_ARGS__)
#define todo(fmt, ...)          log_msg   (LOG_TODO,        fmt, ##__VA_ARGS__)
#define todoln(fmt, ...)        log_msgln (LOG_TODO,        fmt, ##__VA_ARGS__)
#define debug(fmt, ...)         log_msg   (LOG_DEBUG,       fmt, ##__VA_ARGS__)
#define debugln(fmt, ...)       log_msgln (LOG_DEBUG,       fmt, ##__VA_ARGS__)

// End Logging 

/// Begin Timing

typedef struct
{
    struct timespec start;
    struct timespec finish;
    struct timespec delta;
} Timer;

#define NS_PER_SECOND 1000000000

static inline void timer_start(Timer *timer) { clock_gettime(CLOCK_MONOTONIC, &timer->start); }

static inline void timer_print(Timer *timer, char *msg)
{
    infoln("%s took %d.%.9ld secs", msg, (int)timer->delta.tv_sec, timer->delta.tv_nsec);
}

void timer_finish(Timer *timer, char *msg)
{
    clock_gettime(CLOCK_MONOTONIC, &timer->finish);
    timer->delta.tv_nsec = timer->finish.tv_nsec - timer->start.tv_nsec;
    timer->delta.tv_sec  = timer->finish.tv_sec -  timer->start.tv_sec;
    timer_print(timer, msg);
}

/// End Timing

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

// copied from nob.h @ Tsoding
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
#define KEYWORD_ELIF   "elif"
#define KEYWORD_ELSE   "else"
#define KEYWORD_WHILE  "while"
#define KEYWORD_FOR    "for"
#define KEYWORD_RETURN "return"
#define KEYWORD_STRUCT "struct"
#define KEYWORD_TRUE   "true"
#define KEYWORD_FALSE  "false"

typedef enum
{
    TOK_EOF,
    TOK_IDENT,
    TOK_IF,
    TOK_ELIF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_RETURN,
    TOK_STRUCT,
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
    TOK_DOT,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_EQUALS,
    TOK_DOUBLE_EQUALS,
    TOK_ARROW,
    TOK_PLUS,
    TOK_STAR,
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
    static_assert(__tok_types_count == 28, "Cover all token types in token_type_as_string");
    switch (type)
    {
        case TOK_EOF:           return "EOF";
        case TOK_IDENT:         return "Identifier";
        case TOK_IF:            return KEYWORD_IF;
        case TOK_ELIF:          return KEYWORD_ELIF;
        case TOK_ELSE:          return KEYWORD_ELSE;
        case TOK_WHILE:         return KEYWORD_WHILE;
        case TOK_FOR:           return KEYWORD_FOR;
        case TOK_RETURN:        return KEYWORD_RETURN;
        case TOK_STRUCT:        return KEYWORD_STRUCT;
        case TOK_TRUE:          return KEYWORD_TRUE;
        case TOK_FALSE:         return KEYWORD_FALSE;
        case TOK_INTEGER:       return "Integer";
        case TOK_STRING:        return "String";
        case TOK_L_PAREN:       return "(";
        case TOK_R_PAREN:       return ")";
        case TOK_L_SQPAREN:     return "[";
        case TOK_R_SQPAREN:     return "]";
        case TOK_L_CUPAREN:     return "{";
        case TOK_R_CUPAREN:     return "}";
        case TOK_COMMA:         return ",";
        case TOK_DOT:           return ".";
        case TOK_COLON:         return ":";
        case TOK_SEMICOLON:     return ";";
        case TOK_DOUBLE_EQUALS: return "==";
        case TOK_EQUALS:        return "=";
        case TOK_ARROW:         return "->";
        case TOK_PLUS:          return "+";
        case TOK_STAR:          return "*";
        default:
            unreachableln("Unknown token type %d in token_type_as_string", type);
            exit(1);
    }
}

static_assert(__tok_types_count == 28, "Cover all token types in token_print");
void token_print(const Token *tok)
{
    switch (tok->type) {
        case TOK_IDENT:
        case TOK_INTEGER:
        case TOK_STRING:
            printf("<%s, '%s'>", token_type_as_string(tok->type), tok->lexeme);
            break;

        case TOK_IF:
        case TOK_ELIF:
        case TOK_ELSE:
        case TOK_WHILE:
        case TOK_FOR:
        case TOK_RETURN:
        case TOK_STRUCT:
        case TOK_TRUE:
        case TOK_FALSE:
        case TOK_EOF:
            printf("<%s>", token_type_as_string(tok->type));
            break;

        case TOK_L_PAREN:
        case TOK_R_PAREN:
        case TOK_L_SQPAREN:
        case TOK_R_SQPAREN:
        case TOK_L_CUPAREN:
        case TOK_R_CUPAREN:
        case TOK_COMMA:
        case TOK_DOT:
        case TOK_COLON:
        case TOK_SEMICOLON:
        case TOK_EQUALS:
        case TOK_DOUBLE_EQUALS:
        case TOK_ARROW:
        case TOK_PLUS:
        case TOK_STAR:
            printf("'%s'", tok->lexeme);
            break;

        default:
            unreachableln("Token type %d in token_print", tok->type);
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
    ASTNode **items;
    size_t count;
    size_t capacity;
} Nodes;

typedef struct
{
    Tokens tokens;
    size_t pos;
    Nodes functions_stack;
} Parser;
static inline void push_function(Parser *p, ASTNode *fn) { da_push(&p->functions_stack, fn); }
static inline ASTNode *pop_function(Parser *p) { return da_pop(&p->functions_stack); }
static inline ASTNode *current_function(Parser *p) { return p->functions_stack.items[p->functions_stack.count-1]; }

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

#define TOK_INVALID ((TokenType)-1)
static_assert(__tok_types_count == 28, "Cover all token types in lexer_get_next_token");
Token lexer_get_next_token(Lexer *lexer)
{
    lexer_eat_spaces(lexer);

    Token token = { .loc = lexer->loc, .type = TOK_INVALID };

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
        
        // TODO: switch on length for perfomance
             if (streq(token.lexeme, KEYWORD_IF))     token.type = TOK_IF;
        else if (streq(token.lexeme, KEYWORD_ELIF))   token.type = TOK_ELIF;
        else if (streq(token.lexeme, KEYWORD_ELSE))   token.type = TOK_ELSE;
        else if (streq(token.lexeme, KEYWORD_WHILE))  token.type = TOK_WHILE;
        else if (streq(token.lexeme, KEYWORD_FOR))    token.type = TOK_FOR;
        else if (streq(token.lexeme, KEYWORD_RETURN)) token.type = TOK_RETURN;
        else if (streq(token.lexeme, KEYWORD_STRUCT)) token.type = TOK_STRUCT;
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
            case '.': token.type = TOK_DOT;       break;
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

    if (token.type == TOK_INVALID) {
      loc_print(token.loc);
      errorln("Invalid token starting with '%c'", c);
      exit(1);
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
    ASTNODE_DECLARATION,
    ASTNODE_ASSIGNMENT,
    ASTNODE_BLOCK,
    ASTNODE_IF,
    ASTNODE_ELIF,
    ASTNODE_ELSE,
    ASTNODE_WHILE,
    ASTNODE_FOR,
    ASTNODE_RETURN,
    ASTNODE_CALL,
    ASTNODE_CALL_STATEMENT,
    ASTNODE_MEMBER_ACCESS,
    ASTNODE_BINOP,

    ASTNODE_IDENT,
    ASTNODE_NUMBER,
    ASTNODE_STRING,
    ASTNODE_BOOL,
    ASTNODE_LIST_LITERAL,

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
    KIND_NOT_DECLARED = 0,
    KIND_NOT_INFERRED,

    KIND_TYPE,
    KIND_PRIMITIVE,
    KIND_LIST,
    KIND_FUNCTION,
    KIND_STRUCT,
    KIND_POINTER,

    __kinds_count
} Kind;

static_assert(__kinds_count == 8, "Cover all kinds in kind_as_string");
char *kind_as_string(Kind kind)
{
    switch (kind) {
    case KIND_NOT_DECLARED: return "Not Declared";
    case KIND_NOT_INFERRED: return "Not Inferred";
    case KIND_TYPE:         return "Type";
    case KIND_PRIMITIVE:    return "Primitive";
    case KIND_LIST:         return "List";
    case KIND_FUNCTION:     return "Function";
    case KIND_STRUCT:       return "Struct";
    case KIND_POINTER:      return "Pointer";
    default:
        unreachableln("Kind %u in kind_as_string", kind);
        exit(1);
    }
}

typedef enum
{
    PRIMITIVE_UNKNOWN = -1,

    PRIMITIVE_VOID = 0,
    PRIMITIVE_I32,
    PRIMITIVE_STRING,
    PRIMITIVE_BOOL,

    __primitive_types_count
} PrimitiveType;

static_assert(__primitive_types_count == 5-1, "Cover all primitive types in primitive_type_as_string");
char *primitive_type_as_string(PrimitiveType type)
{
    switch (type) {
    case PRIMITIVE_VOID:   return "void";
    case PRIMITIVE_I32:    return "int";
    case PRIMITIVE_STRING: return "string";
    case PRIMITIVE_BOOL:   return "bool";
    default:
        unreachableln("Type %u in primitive_type_as_string", type);
        exit(1);
    }
}

static_assert(__primitive_types_count == 5-1, "Cover all primitive types in primitive_type_to_c");
char *primitive_type_to_c(PrimitiveType type)
{
    switch (type) {
    case PRIMITIVE_VOID:   return "void";
    case PRIMITIVE_I32:    return "int32_t";
    case PRIMITIVE_STRING: return "char *";
    case PRIMITIVE_BOOL:   return "int";
    default:
        unreachableln("Type %u in primitive_type_to_c", type);
        exit(1);
    }
};

typedef struct TypeInfo TypeInfo;
typedef struct
{
    TypeInfo **items;
    size_t count;
    size_t capacity;
} TypeInfos;

typedef struct Symbol Symbol;
typedef struct Symbols
{
    Symbol **items;
    size_t count;
    size_t capacity;
} Symbols;

static_assert(__kinds_count == 8, "Cover all kinds in TypeInfo struct");
typedef struct TypeInfo
{
    Kind kind;
    char name[64];
    union {
        TypeInfo *type; // TYPE
        PrimitiveType primitive; // PRIMITIVE
        struct {
            TypeInfos params;
            struct TypeInfo *ret_type;
            bool is_method; // struct function
            bool is_static; // struct function without self
        } fn; // FUNCTION
        struct {
            TypeInfo *elts_type;
            bool is_sized;
            size_t size;
        } list; // LIST
        struct {
            Symbols members;
        } strct; // STRUCT
        TypeInfo *pointed_type; // POINTER
    };
} TypeInfo;

void type_print(TypeInfo *type);

static_assert(__kinds_count == 8, "Cover all kinds in generate_type");
void generate_type(TypeInfo *type, char *name, FILE *f)
{
    switch (type->kind) {
    case KIND_NOT_DECLARED:
    case KIND_NOT_INFERRED:
        unreachableln("Type checking should have resolved KIND_NOT_DECLARED and KIND_NOT_INFERRED");
        exit(1);

    case KIND_TYPE:
        todo("Generate kind %s -> ", kind_as_string(KIND_TYPE));
        type_print(type->type);
        printf("\n");
        exit(1);
        break;

    case KIND_PRIMITIVE:
        fprintf(f, "%s", primitive_type_to_c(type->primitive));
        break;

    case KIND_LIST: {
        if (type->list.is_sized) {
            generate_type(type->list.elts_type, NULL, f);
            if (name) fprintf(f, " %s", name);
            fprintf(f, "[%zu]", type->list.size);
        } else {
            todoln("Dynamic lists are not yet supported");
            exit(1);
        }
                    } break;

    case KIND_FUNCTION: {
        todoln("Write function to c");
        exit(1);
    } break;

    case KIND_STRUCT: {
        fprintf(f, "%s", type->name);
        break;
    } break;

    case KIND_POINTER: {
        generate_type(type->pointed_type, name, f);
        fprintf(f, "*");
        break;
    } break;

    default:
        unreachableln("Kind %u in generate_type", type->kind);
        exit(1);
    }
}

bool _are_types_equal(TypeInfo *a, TypeInfo *b, bool report_errors, Location *loc_a, Location *loc_b)
{
    // TODO: report locations (maybe pass symbol for name?)
    (void)loc_a;
    (void)loc_b;

    //debug("Comparing types: '");
    //type_print(a);
    //printf("' and '");
    //type_print(b);
    //printf("'\n");

    if (a == NULL || b == NULL) {
        unreachableln("One of the types is NULL in _are_types_equal");
        exit(1);
    }

    if (a == b) return true;

    if (a->kind != b->kind) {
        if (report_errors) {
            errorln("Types have different kinds '%s' and '%s'", kind_as_string(a->kind), kind_as_string(b->kind));
        }
        return false;
    }

    static_assert(__kinds_count == 8, "Cover all kinds in _are_types_equal");
    switch (a->kind) {
    case KIND_NOT_DECLARED: return true;
    case KIND_NOT_INFERRED: return true;
    case KIND_TYPE:         return _are_types_equal(a->type, b->type, report_errors, loc_a, loc_b);
    case KIND_PRIMITIVE:    return a->primitive == b->primitive;
    case KIND_LIST: {
        if (!_are_types_equal(a->list.elts_type, b->list.elts_type, report_errors, loc_a, loc_b)) {
            if (report_errors) {
                error("Lists have different elements types '");
                type_print(a->list.elts_type);
                printf("' and '");
                type_print(b->list.elts_type);
                printf("'\n");
            }
            return false;
        }
        if (a->list.is_sized && b->list.is_sized) {
            if (a->list.size != b->list.size) {
                if (report_errors) {
                    errorln("Sized lists have different size: %zu and %zu", a->list.size, b->list.size);
                }
                return false;
            }
        } else if (!(!a->list.is_sized && !b->list.is_sized)) {
            if (report_errors) {
                errorln("Lists sizedeness mismatch: %s and %s",
                        a->list.is_sized ? "sized" : "dynamic",
                        b->list.is_sized ? "sized" : "dynamic");
            }
            return false;
        }
    } break;
    case KIND_FUNCTION: {
        size_t count_a = a->fn.params.count;
        size_t count_b = b->fn.params.count;
        if (count_a != count_b) {
            if (report_errors) {
                errorln("Functions have different number of arguments '%zu' and '%zu'", count_a, count_b);
            }
            return false;
        }
        TypeInfo *ret_a = a->fn.ret_type;
        TypeInfo *ret_b = b->fn.ret_type;
        if (!_are_types_equal(ret_a, ret_b, report_errors, loc_a, loc_b)) {
            if (report_errors) {
                error("Functions have different return type '");
                type_print(ret_a);
                printf("' and '");
                type_print(ret_b);
                printf("'\n");
            }
            return false;
        }
        for (size_t i = 0; i < a->fn.params.count; i++) {
            TypeInfo *pa = a->fn.params.items[i];    
            TypeInfo *pb = b->fn.params.items[i];    
            if (!_are_types_equal(pa, pb, report_errors, loc_a, loc_b)) {
                if (report_errors) {
                    error("Function have different arguments in position %zu: '", i+1);
                    type_print(pa);
                    printf("' and '");
                    type_print(pa);
                    printf("'\n");
                }
                return false;
            }
        }
    } break;
    case KIND_STRUCT: {
        if (!streq(a->name, b->name)) {
            if (report_errors) {
                errorln("Structs have different names '%s' and '%s'", a->name, b->name);
            }
            return false;
        }
        //todoln("Check that structs match fields"); // TODO: I don't think I need that
                                                     //       until I implement some kind of subtyping
        //exit(1);
    } break;

    case KIND_POINTER: return _are_types_equal(a->pointed_type, b->pointed_type, report_errors, loc_a, loc_b);

    default:
        unreachableln("Kind %u in _are_types_equal", a->kind);
        exit(1);
    }
    return true;
}
static inline bool are_types_equal_with_errors(TypeInfo *a, TypeInfo *b, Location *loc_a, Location *loc_b)
{
    return _are_types_equal(a, b, true, loc_a, loc_b);
}
static inline bool are_types_equal(TypeInfo *a, TypeInfo *b) { return _are_types_equal(a, b, false, NULL, NULL); }

static inline TypeInfo *primitive_type(PrimitiveType type);

static_assert(__kinds_count == 8, "Cover all kinds in type_print");
void type_print(TypeInfo *type)
{
    switch (type->kind) {
    case KIND_NOT_DECLARED:
    case KIND_NOT_INFERRED:
        printf("%s", kind_as_string(type->kind));
        break;

    case KIND_TYPE:
        printf("Type(");
        type_print(type->type);
        printf(")");
        break;

    case KIND_PRIMITIVE:
        printf("%s", primitive_type_as_string(type->primitive));
        break;

    case KIND_LIST:
        printf("[");
        type_print(type->list.elts_type);
        if (type->list.is_sized) printf(", %zu", type->list.size);
        printf("]");
        break;

    case KIND_FUNCTION: {
        printf("(");
        for (size_t i = 0; i < type->fn.params.count; i++) {
            type_print(type->fn.params.items[i]);
            if (i < type->fn.params.count-1) printf(", ");
        }
        printf(")");
        printf(" -> ");
        type_print(type->fn.ret_type);
    } break;

    case KIND_STRUCT:
        printf("%s", type->name);
        break;

    case KIND_POINTER:
        printf("*");
        type_print(type->pointed_type);
        break;

    default:
        unreachableln("Kind %u in type_print", type->kind);
        exit(1);
    } 
}

TypeInfo *create_type(Kind kind, const char *name)
{
    TypeInfo *type = calloc(1, sizeof(TypeInfo));
    assert(type);

    type->kind = kind;
    if (name) strcpy(type->name, name);
    return type;
}
static inline  TypeInfo *create_type_without_name(Kind kind) { return create_type(kind, NULL); }

static_assert(__primitive_types_count == 5-1, "Cover all primitive types in PrimitiveValue union");
typedef union
{
    char string[64];
    bool _bool;
    int32_t _int;
} PrimitiveValue;

typedef enum
{
    SYM_VARIABLE,
    SYM_FUNCTION,
    SYM_TYPE,

    __symbol_kinds_count
} SymbolKind;

char *symbol_kind_as_string(SymbolKind kind)
{
    switch (kind) {
    case SYM_VARIABLE: return "Variable";
    case SYM_FUNCTION: return "Function";
    case SYM_TYPE:     return "Type";
    default: unreachableln("SymbolKind %u in symbol_kind_as_string", kind); exit(1);
    }
}

struct Symbol
{
    SymbolKind kind;
    TypeInfo *type;
    Location loc;
    char prefix[64];
    char name[64];
    union {
        struct { // SYM_VARIABLE
            bool initialized;
        } var;
        struct { // SYM_FUNCTION
            Symbols params;
        } fn;
        struct { // SYM_TYPE - struct
            Symbols members;
        } strct;
    };
};

Symbol *create_symbol(SymbolKind kind, const char *prefix, const char *name, Location loc)
{
    Symbol *sym = calloc(1, sizeof(Symbol));
    assert(sym);

    sym->kind = kind;
    sym->loc = loc;
    if (prefix) strcpy(sym->prefix, prefix);
    strcpy(sym->name, name);
    return sym;
}
static inline Symbol *create_symbol_from_token(SymbolKind kind, Token *token)
{
    return create_symbol(kind, NULL, token->lexeme, token->loc);
}
static inline Symbol *create_symbol_from_token_with_prefix(SymbolKind kind, Token *token, const char *prefix)
{
    return create_symbol(kind, prefix, token->lexeme, token->loc);
}

typedef struct
{
    Symbols symbols;
} Scope;

typedef struct
{
    Scope *items;
    size_t count;
    size_t capacity;
} Scopes;
static Scopes scopes = {0};
static inline void push_scope(Scope scope) { da_push(&scopes, scope); }
static inline void push_new_scope(void) { push_scope((Scope){0}); }
static inline Scope pop_scope(void) { return da_pop(&scopes); }
static inline Scope *get_global_scope(void) { return &scopes.items[0]; }
static inline bool in_global_scope(void) { return scopes.count == 1; }

static_assert(__primitive_types_count == 5-1, "Declare all primitive types and retrieve them in primitive_type");
static TypeInfo _type_primitive_void;
static TypeInfo _type_primitive_i32;
static TypeInfo _type_primitive_string;
static TypeInfo _type_primitive_bool;
static inline TypeInfo *primitive_type(PrimitiveType type)
{
    switch (type) {
    case PRIMITIVE_VOID:   return &_type_primitive_void;
    case PRIMITIVE_I32:    return &_type_primitive_i32;
    case PRIMITIVE_STRING: return &_type_primitive_string;
    case PRIMITIVE_BOOL:   return &_type_primitive_bool;
    default:
        unreachableln("Primitive type %u in primitive_type", type);
        exit(1);
    }
}

#define MAIN_FN_ALTERNATIVES (4)
static TypeInfo *_types_main_fn[MAIN_FN_ALTERNATIVES] = {0};

Symbol *get_symbol(char *name)
{
    int i = scopes.count-1;
    while (i >= 0) {
        Symbols *symbols = &scopes.items[i].symbols;
        for (size_t j = 0; j < symbols->count; j++) {
            Symbol *sym = symbols->items[j];
            if (streq(name, sym->name)) return sym;
        }
        i--;
    }
    return NULL;
}

Symbol *_get_symbol(const char *name, SymbolKind kind)
{
    int i = scopes.count-1;
    while (i >= 0) {
        Symbols *symbols = &scopes.items[i].symbols;
        for (size_t j = 0; j < symbols->count; j++) {
            Symbol *sym = symbols->items[j];
            if (sym->kind == kind && streq(name, sym->name)) return sym;
        }
        i--;
    }
    return NULL;
}

static inline Symbol *get_variable_symbol(const char *name) { return _get_symbol(name, SYM_VARIABLE); }
static inline Symbol *get_function_symbol(const char *name) { return _get_symbol(name, SYM_FUNCTION); }
static inline Symbol *get_type_symbol(const char *name) { return _get_symbol(name, SYM_TYPE); }

typedef struct ASTNode
{
    ASTNodeType type;
    Location loc;
    char name[64];

    TypeInfo *resolved_type;

    static_assert(__astnode_types_count == 19, "Cover all ast node types ASTNode struct");
    union {
        Symbol *ident; // IDENT
        PrimitiveValue value; // NUMBER, STRING, BOOL
        struct {
            Operator op;
            ASTNode *left;
            ASTNode *right;
        } binary; // BINOP
        struct {
            ASTNode *expr;
            ASTNode *block;
            ASTNode *list; // elif list or else (for IF and ELIS)
        } conditional; // IF, ELIF, WHILE
        ASTNode *statements; // BLOCK
        struct {
            size_t size;
            TypeInfo *elts_type;
            ASTNode *head;
        } list; // LIST_LITERAL
        struct {
            ASTNode *fn;
            ASTNode *expr;
        } ret; // RETURN
        ASTNode *accessed_struct; //MEMBER_ACCESS,
        struct {
            ASTNode *callee;
            ASTNode *args;
        } call; // CALL, CALL_STATEMENT
        struct {
            ASTNode *lhs;
            ASTNode *rhs;
            bool is_init;
        } assign; // ASSIGNMENT
        struct {
            Symbol *sym;
            union {
                struct {
                    ASTNode *init; // it's an ASSIGNMENT node where lhs is DECLARATION node and rhs is expr
                } var;
                struct {
                    ASTNode *params;
                    ASTNode *block;
                    ASTNode *ret;
                } fn;
                struct {
                    ASTNode *members;
                } strct;
            };
        } decl; // DECLARATION
    };
    ASTNode *next; // all statements, lists
} ASTNode;

ASTNode *create_node(ASTNodeType type, const char *name, Location loc)
{
    ASTNode *node = calloc(1, sizeof(ASTNode));
    assert(node);
    
    node->type = type;
    node->loc = loc;
    if (name) strcpy(node->name, name);
    return node;
}
static inline ASTNode *create_node_from_token(ASTNodeType type, const Token *token)
{
    return create_node(type, token->lexeme, token->loc);
}
static inline ASTNode *create_node_without_name(ASTNodeType type, Location loc) { return create_node(type, NULL, loc); }

typedef ASTNode *(*PrefixExprFn)(Parser *);
typedef ASTNode *(*InfixExprFn)(Parser *, ASTNode *left);

typedef enum {
    PREC_NONE   = 0,
    PREC_TERM   = 10, // + -
    PREC_FACTOR = 20, // * /
    PREC_CALL   = 30, // ( .
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

Token *parser_expect(Parser *parser, TokenType type)
{
    Token *token = current_token;
    if (token->type == type) {
        parser_advance(parser);
        return token;
    } else {
        loc_print(current_token->loc);
        error("Expecting '%s', but got ", token_type_as_string(type));
        token_print(token);
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
    ASTNode *node = create_node_from_token(ASTNODE_NUMBER, current_token);
    node->value._int = atoi(current_token->lexeme);
    parser_advance(parser);
    return node;
}

ASTNode *parse_string(Parser *parser)
{
    ASTNode *node = create_node_from_token(ASTNODE_STRING, current_token);
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
        token_print(current_token);
        printf("\n");
        exit(1);
    }
    parser_expect(parser, TOK_R_PAREN);
    return node;
}

//static_assert(__types_count == 1, "Cover all types in size_of_type");
//size_t size_of_type(Type type)
//{
//    switch (type)
//    {
//        case TYPE_I32: return sizeof(int32_t);
//        default: 
//            errorln("Unreachable type %u in size_of_type", type);
//            exit(1);
//    }
//}

static_assert(__primitive_types_count == 5-1, "Cover all primitive types in primitive_type_from_string");
PrimitiveType primitive_type_from_string(const char *type_string)
{
         if (streq(type_string, primitive_type_as_string(PRIMITIVE_VOID)))   return PRIMITIVE_VOID;
    else if (streq(type_string, primitive_type_as_string(PRIMITIVE_I32)))    return PRIMITIVE_I32;
    else if (streq(type_string, primitive_type_as_string(PRIMITIVE_STRING))) return PRIMITIVE_STRING;
    else if (streq(type_string, primitive_type_as_string(PRIMITIVE_BOOL)))   return PRIMITIVE_BOOL;
    else                                                                     return PRIMITIVE_UNKNOWN;
}

TypeInfo *get_type(const char *name)
{
    PrimitiveType prim_type = primitive_type_from_string(name); 
    if (prim_type != PRIMITIVE_UNKNOWN) return primitive_type(prim_type);

    Symbol *named_type = get_type_symbol(name);
    if (named_type) return named_type->type;

    return create_type(KIND_NOT_DECLARED, name);
}

static_assert(__kinds_count == 8, "Cover all kinds in parse_type");
TypeInfo *parse_type(Parser *parser)
{
    if (current_token->type == TOK_IDENT) { // KIND_PRIMITIVE, KIND_STRUCT
        TypeInfo *type = get_type(current_token->lexeme);
        parser_advance(parser); // type
        return type;
    } else if (current_token->type == TOK_L_SQPAREN) { // KIND_LIST
        TypeInfo *list_type = create_type_without_name(KIND_LIST);
        parser_advance(parser); // "["
        list_type->list.elts_type = parse_type(parser);

        if (current_token->type == TOK_COMMA) { // fixed size list
            parser_advance(parser); // ","
            Token *size_token = parser_expect(parser, TOK_INTEGER);
            list_type->list.is_sized = true;
            list_type->list.size = atoi(size_token->lexeme);
        }
        
        parser_expect(parser, TOK_R_SQPAREN);
        return list_type;
    } else if (current_token->type == TOK_L_PAREN) { // KIND_FUNCTION
        todoln("Parse function type");
        exit(1);
    } else if (current_token->type == TOK_STAR) { // KIND_POINTER
        TypeInfo *pointer_type = create_type_without_name(KIND_POINTER);
        parser_advance(parser); // "*"
        pointer_type->pointed_type = parse_type(parser);
        return pointer_type;
    } else {
        loc_print(current_token->loc);
        error("Expecting a type, but got ");
        token_print(current_token);
        printf("\n");
        exit(1);
    }
}

ASTNode *parse_statement(Parser *parser);

ASTNode *parse_block(Parser *parser)
{
    push_new_scope();
    
    parser_advance(parser); // '{'

    ASTNode *node = create_node_without_name(ASTNODE_BLOCK, current_token->loc);

    if (current_token->type == TOK_R_CUPAREN) { // empty block, node->statements is NULL, no need to create a new scope
        goto ret;
    }

    node->statements = parse_statement(parser); 
    if (node->statements) {
        ASTNode *current_node = node->statements;
        while (current_token->type != TOK_R_CUPAREN) {
            current_node->next = parse_statement(parser);
            if (current_node->next) current_node = current_node->next;
        }
    }

ret:
    parser_expect(parser, TOK_R_CUPAREN);
    pop_scope();

    return node;
}

static inline ASTNode *parse_declaration(Parser *parser);
void _parse_function_declaration(Parser *parser, ASTNode *node, Symbol *sym, const char *prefix)
{
    sym->type = create_type(KIND_FUNCTION, sym->name);
    sym->fn.params = (Symbols){0};
    sym->type->fn.params = (TypeInfos){0};

    push_function(parser, node);

    parser_expect(parser, TOK_L_PAREN);

    ASTNode **next_param = &node->decl.fn.params;
    Symbols params_symbols = {0};
    TypeInfos params_types = {0};

    bool done = false;
    if (prefix) {
        sym->type->fn.is_method = true;
        if (!streq(current_token->lexeme, "self")) {
            sym->type->fn.is_static = true;
        } else {
            ASTNode *self_node = create_node_from_token(ASTNODE_DECLARATION, current_token);
            Symbol *self_sym = create_symbol_from_token(SYM_VARIABLE, current_token);
            self_sym->type = get_type(prefix);

            da_push(&params_symbols, self_sym);
            da_push(&params_types, self_sym->type);

            self_node->decl.sym = self_sym;
            *next_param = self_node;
            next_param = &self_node->next;

            parser_advance(parser); // "self"
            if (current_token->type == TOK_R_PAREN) done = true;
            else parser_expect(parser, TOK_COMMA);
        }
    }

    while (current_token->type != TOK_R_PAREN && !done) {
        ASTNode *param = parse_declaration(parser);

        if (param->decl.sym->kind != SYM_VARIABLE) {
            loc_print(param->loc);
            error("Cannot declare %s as function parameter, only variables are allowed",
                    symbol_kind_as_string(param->decl.sym->kind));
            exit(1);
        }

        da_push(&params_symbols, param->decl.sym);
        da_push(&params_types, param->decl.sym->type);
        *next_param = param;

        next_param = &param->next;

        if (current_token->type == TOK_R_PAREN) break;
        else if (current_token->type == TOK_COMMA) parser_advance(parser); // ","
        else {
            loc_print(current_token->loc);
            error("Expected ',' or ')' in function parameters list, but got ");
            token_print(current_token);
            printf("\n");
            exit(1);
        }
    }
    sym->fn.params = params_symbols;
    sym->type->fn.params = params_types;

    parser_expect(parser, TOK_R_PAREN);

    if (current_token->type == TOK_ARROW) {
        parser_advance(parser); // "->"
        sym->type->fn.ret_type = parse_type(parser);
    } else {
        sym->type->fn.ret_type = primitive_type(PRIMITIVE_VOID);
    }

    parser_expect(parser, TOK_EQUALS);

    // TODO: maybe if current != { we can parse a single statement, like in if, but at least here we got = to divide
    // TODO: functions with a single return statement (params) => expr (can be inferred) (used also for lambdas)
    node->decl.fn.block = parse_block(parser);

    pop_function(parser);
}

static inline ASTNode *parse_declaration_statement_with_prefix(Parser *parser, const char *prefix);
void _parse_struct_declaration(Parser *parser, ASTNode *node, Symbol *sym)
{
    sym->type = create_type(KIND_STRUCT, sym->name);
    sym->strct.members = (Symbols){0};
    sym->type->strct.members = (Symbols){0};
    
    parser_advance(parser); // "struct"
    parser_expect(parser, TOK_EQUALS);

    // TODO: should I push/pop new scope?
    parser_expect(parser, TOK_L_CUPAREN);

    ASTNode **next_member = &node->decl.strct.members;
    Symbols members = {0};
    while (current_token->type != TOK_R_CUPAREN) {
        ASTNode *member = parse_declaration_statement_with_prefix(parser, sym->name);
        da_push(&members, member->decl.sym);
        *next_member = member;
        next_member = &member->next;
    }
    parser_expect(parser, TOK_R_CUPAREN);

    sym->strct.members = members;
    sym->type->strct.members = members;
}

ASTNode *_parse_variable_declaration(Parser *parser, Symbol *sym)
{
    if (current_token->type == TOK_EQUALS) {
        sym->type = create_type_without_name(KIND_NOT_INFERRED);
    } else {
        sym->type = parse_type(parser);
    }

    ASTNode *var_init_expr = NULL;

    if (current_token->type == TOK_EQUALS) {
        parser_advance(parser); // "="
        sym->var.initialized = true;
        var_init_expr = parse_expression(parser);
        if (!var_init_expr) {
            loc_print(current_token->loc);
            error("Expected expression in variable declaration, but got ");
            token_print(current_token);
            printf("\n");
            exit(1);
        }
    }

    return var_init_expr;
}

ASTNode *_parse_declaration(Parser *parser, bool is_statement, const char *prefix)
{
    ASTNode *node = create_node_from_token(ASTNODE_DECLARATION, current_token);
    Symbol *sym = create_symbol_from_token_with_prefix(SYM_VARIABLE, current_token, prefix);

    parser_advance(parser); // symbol name
    parser_expect(parser, TOK_COLON);

    if (current_token->type == TOK_STRUCT) {
        sym->kind = SYM_TYPE;
        _parse_struct_declaration(parser, node, sym);
    } else if (current_token->type == TOK_L_PAREN) {
        sym->kind = SYM_FUNCTION;
        _parse_function_declaration(parser, node, sym, prefix);
    } else {
        ASTNode *var_init_expr = _parse_variable_declaration(parser, sym);
        if (var_init_expr) {
            ASTNode *lhs = create_node(ASTNODE_IDENT, sym->name, node->loc);
            lhs->ident = sym;

            ASTNode *assign = create_node_without_name(ASTNODE_ASSIGNMENT, var_init_expr->loc);
            assign->assign.lhs = lhs;
            assign->assign.rhs = var_init_expr;
            assign->assign.is_init = true;

            node->decl.var.init = assign;
        }
        if (is_statement) parser_expect(parser, TOK_SEMICOLON);
    }

    node->decl.sym = sym;
    return node;
}
static inline ASTNode *parse_declaration_statement(Parser *parser) { return _parse_declaration(parser, true, NULL); } 
static inline ASTNode *parse_declaration(Parser *parser) { return _parse_declaration(parser, false, NULL); } 
static inline ASTNode *parse_declaration_statement_with_prefix(Parser *parser, const char *prefix)
{
    return _parse_declaration(parser, true, prefix);
}

ASTNode *parse_true(Parser *parser)
{
    ASTNode *node = create_node_from_token(ASTNODE_BOOL, current_token);
    node->value._bool = true;
    parser_advance(parser);
    return node;
}
ASTNode *parse_false(Parser *parser)
{
    ASTNode *node = create_node_from_token(ASTNODE_BOOL, current_token);
    node->value._bool = false;
    parser_advance(parser);
    return node;
}

ASTNode *parse_else(Parser *parser)
{
    ASTNode *node = create_node_from_token(ASTNODE_ELSE, current_token);
    parser_advance(parser); // "else"
    node->conditional.block = current_token->type == TOK_L_CUPAREN ? parse_block(parser) : parse_statement(parser);
    return node;
}

static inline ASTNode *parse_elif(Parser *parser);

ASTNode *_parse_if_or_elif(Parser *parser, bool is_if)
{
    ASTNode *node = create_node_from_token(is_if ? ASTNODE_IF : ASTNODE_ELIF, current_token);

    parser_advance(parser); // "if" or "elif"
    
    node->conditional.expr = parse_expression(parser);
    if (!node->conditional.expr) {
        loc_print(current_token->loc);
        error("Expected boolean expression, but got ");
        token_print(current_token);
        printf("\n");
        exit(1);
    }

    if (current_token->type == TOK_L_CUPAREN) {
        node->conditional.block = parse_block(parser);
    } else if (current_token->type == TOK_COLON) {
        parser_advance(parser); // ':'
        ASTNode *block = create_node_without_name(ASTNODE_BLOCK, current_token->loc);
        block->statements = parse_statement(parser);
        node->conditional.block = block;
    } else {
        loc_print(current_token->loc);
        error("Expecting block or ':', but got ");
        token_print(current_token);
        printf("\n");
        exit(1);
    }

    ASTNode *cont_node = NULL;
    if (current_token->type == TOK_ELIF) {
        cont_node = parse_elif(parser);
    } else if (current_token->type == TOK_ELSE) {
        cont_node = parse_else(parser);
    }

    if (is_if) node->conditional.list = cont_node;
    else       node->next             = cont_node;

    return node;
}

static inline ASTNode *parse_if(Parser *parser) { return _parse_if_or_elif(parser, true); }
static inline ASTNode *parse_elif(Parser *parser) { return _parse_if_or_elif(parser, false); }

ASTNode *parse_while(Parser *parser)
{
    ASTNode *node = create_node_from_token(ASTNODE_WHILE, current_token);
    parser_advance(parser); // "while"

    node->conditional.expr = parse_expression(parser);
    if (!node->conditional.expr) {
        loc_print(current_token->loc);
        error("Expected boolean expression, but got ");
        token_print(current_token);
        printf("\n");
        exit(1);
    }

    if (current_token->type == TOK_L_CUPAREN) {
        node->conditional.block = parse_block(parser);
    } else if (current_token->type == TOK_COLON) {
        parser_advance(parser); // ':'
        ASTNode *block = create_node_without_name(ASTNODE_BLOCK, current_token->loc);
        block->statements = parse_statement(parser);
        node->conditional.block = block;
    } else {
        loc_print(current_token->loc);
        error("Expecting block or ':', but got ");
        token_print(current_token);
        printf("\n");
        exit(1);
    }

    return node;
}

ASTNode *parse_for(Parser *parser)
{
    ASTNode *node = create_node_from_token(ASTNODE_FOR, current_token);
    parser_advance(parser); // "for"

    todoln("Parse for statement");
    exit(1);

    return node;
}

ASTNode *parse_return(Parser *parser)
{
    if (da_is_empty(&parser->functions_stack)) {
        loc_print(current_token->loc);
        errorln("Unexpected return outside of function");
        exit(1);
    }

    ASTNode *node = create_node_from_token(ASTNODE_RETURN, current_token);
    node->ret.fn = current_function(parser);

    parser_advance(parser); // "return"

    if (current_token->type != TOK_SEMICOLON) {
        node->ret.expr = parse_expression(parser);
        if (node->ret.expr) {
            node->ret.fn->decl.fn.ret = node;
        }
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

ASTNode *parse_call_infix(Parser *parser, ASTNode *left)
{
    parser_expect(parser, TOK_L_PAREN);

    ASTNode *node = create_node(ASTNODE_CALL, left->name, left->loc); // TODO: left->loc ?
    node->call.callee = left;

    if (current_token->type == TOK_R_PAREN) {
        node->call.args = parse_expression_list(parser);
    }
    parser_expect(parser, TOK_R_PAREN);

    return node;
}

ASTNode *parse_member_access(Parser *parser, ASTNode *left)
{
    parser_expect(parser, TOK_DOT);

    if (current_token->type != TOK_IDENT) {
        loc_print(current_token->loc);
        errorln("Expected member name after '.'");
        exit(1);
    }
    
    ASTNode *node = create_node(ASTNODE_MEMBER_ACCESS, current_token->lexeme, left->loc); // TODO: left->loc ? 
    node->accessed_struct = left;
    
    parser_advance(parser); // member name
    
    return node;
}

ASTNode *parse_ident(Parser *parser)
{
    ASTNode *node = create_node_from_token(ASTNODE_IDENT, current_token);
    parser_advance(parser); // ident name
    return node;
}

ASTNode *parse_assignment(Parser *parser, ASTNode *l_value)
{
    parser_expect(parser, TOK_EQUALS);

    ASTNode *node = create_node(ASTNODE_ASSIGNMENT, current_token->lexeme, l_value->loc);
    node->assign.lhs = l_value;

    node->assign.rhs = parse_expression(parser);
    if (!node->assign.rhs) {
        loc_print(current_token->loc);
        error("Expected expression after assignment, but got ");
        token_print(current_token);
        printf("\n");
        exit(1);
    }

    parser_expect(parser, TOK_SEMICOLON);
    return node;
}

ASTNode *parse_binary(Parser *parser, ASTNode *left)
{
    Token *op_token = current_token;
    ASTNode *node = create_node_from_token(ASTNODE_BINOP, current_token);
    node->binary.op = operator_from_token_type(op_token->type);
    parser_advance(parser);
    ExprRule *rule = get_expression_rule(op_token->type);
    node->binary.left = left;
    node->binary.right = parse_expression_with_precedence(parser, rule->precedence);
    if (!node->binary.right) {
        loc_print(current_token->loc);
        error("Expected expression, but got ");
        token_print(current_token);
        printf("\n");
        exit(1);
    }
    return node;
}

size_t node_list_length(ASTNode *list)
{
    size_t len = 0;
    ASTNode *current = list;
    while (current) {
        len++;
        current = current->next;
    }
    return len;
}

ASTNode *parse_list_literal(Parser *parser)
{
    parser_expect(parser, TOK_L_SQPAREN);

    ASTNode *node = create_node_without_name(ASTNODE_LIST_LITERAL, current_token->loc);
    node->list.head = parse_expression_list(parser);

    size_t size = node_list_length(node->list.head);
    node->list.size = size;
    node->list.elts_type = create_type_without_name(size > 0 ? KIND_NOT_DECLARED : KIND_NOT_INFERRED);

    parser_expect(parser, TOK_R_SQPAREN);

    return node;
}

static_assert(__tok_types_count == 28, "Cover all token types in expression_rules table");
ExprRule expression_rules[__tok_types_count] = {
    // No expression rules related to these tokens
    [TOK_IF]         = {NULL, NULL, PREC_NONE},
    [TOK_ELIF]       = {NULL, NULL, PREC_NONE},
    [TOK_ELSE]       = {NULL, NULL, PREC_NONE},
    [TOK_WHILE]      = {NULL, NULL, PREC_NONE},
    [TOK_FOR]        = {NULL, NULL, PREC_NONE},
    [TOK_RETURN]     = {NULL, NULL, PREC_NONE},
    [TOK_STRUCT]     = {NULL, NULL, PREC_NONE},
    [TOK_R_PAREN]    = {NULL, NULL, PREC_NONE},
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
    [TOK_L_SQPAREN]  = {parse_list_literal, NULL, PREC_NONE}, // TODO: what prec?

    // Arithmetic operators
    [TOK_PLUS]    = {NULL, parse_binary, PREC_TERM},
    [TOK_STAR]    = {NULL, parse_binary, PREC_FACTOR},

    // Logical operators
    [TOK_DOUBLE_EQUALS] = {NULL, parse_binary, PREC_TERM},

    // Grouping and others
    [TOK_L_PAREN] = {parse_grouping, parse_call_infix,   PREC_CALL},
    [TOK_DOT]     = {NULL,           parse_member_access, PREC_CALL},
};

static_assert(__tok_types_count == 28, "Cover all token types in parse_statement");
static_assert(__astnode_types_count == 19, "Cover all ast node types in parsing");
ASTNode *parse_statement(Parser *parser)
{
    Token *token = current_token;

    if (in_global_scope() && !(token->type == TOK_IDENT && parser_peek(parser)->type == TOK_COLON)) {
        // TODO: report this error with some sense
        loc_print(token->loc);
        errorln("Only declarations are allowed at top level");
        exit(1);
    }

    switch (token->type) {
        case TOK_IF:        return parse_if(parser);
        case TOK_WHILE:     return parse_while(parser);
        case TOK_FOR:       return parse_for(parser);
        case TOK_RETURN:    return parse_return(parser);
        case TOK_L_CUPAREN: return parse_block(parser);

        case TOK_IDENT: {
            if (parser_peek(parser)->type == TOK_COLON) {
                return parse_declaration_statement(parser);
            }

            ASTNode *expr = parse_expression(parser);

            if (current_token->type == TOK_EQUALS) {
                return parse_assignment(parser, expr);
            }

            parser_expect(parser, TOK_SEMICOLON);
            expr->type = ASTNODE_CALL_STATEMENT;
            return expr;
        };

        default:
            // TODO report this error correctly
            loc_print(token->loc);
            error("Unexpected ");
            token_print(token);
            printf(" starting a statement\n");
            //loc_print(token->loc);
            //noteln("A statement is a declaration, an assignment, a function call or a conditional");
            exit(1);
    }
}

ASTNode *parse_program(Parser *parser)
{
    ASTNode *ast = create_node_without_name(ASTNODE_PROGRAM, current_token->loc);
    ASTNode *node = ast;

    push_new_scope(); // global scope

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

void usage(char *program_name)
{
    (void) program_name;
    todoln("usage");
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

bool compile_c(void)
{
    char *gcc_cmd[] = {
        "gcc",
        "-o", "kjude_output",
        DEFAULT_KJUDE_INTERMEDIATE_C_FILE,
        "-Wall", "-Wextra",
        "-std=c99", "-pedantic", //"-pedantic-errors",
        NULL
    };
    return run_cmd(gcc_cmd);
}

Symbol *get_struct_member(TypeInfo *struct_type, const char *member_name)
{
    for (size_t i = 0; i < struct_type->strct.members.count; i++) {
        Symbol *member = struct_type->strct.members.items[i]; 
        if (streq(member->name, member_name)) {
            return member;
        }
    }
    return NULL;
}

TypeInfo *binop_type(ASTNode *node);

static_assert(__astnode_types_count == 19, "Cover all ast node types in node_type");
TypeInfo *node_type(ASTNode *node)
{
    if (!node) {
        unreachableln("NULL node in node_type");
        exit(1);
    }

    if (node->resolved_type && node->resolved_type->kind != KIND_NOT_INFERRED)
        return node->resolved_type;

    TypeInfo *result_type = NULL;

    switch (node->type) {

    case ASTNODE_PROGRAM:
    case ASTNODE_DECLARATION:
    case ASTNODE_ASSIGNMENT:
    case ASTNODE_BLOCK:
    case ASTNODE_IF:
    case ASTNODE_ELIF:
    case ASTNODE_ELSE:
    case ASTNODE_WHILE:
    case ASTNODE_FOR:
    case ASTNODE_RETURN: {
        unreachableln("Node %u in node_type", node->type);
        exit(1);
    }

    case ASTNODE_MEMBER_ACCESS: {
        TypeInfo *struct_type = node_type(node->accessed_struct);
        result_type = get_struct_member(struct_type, node->name)->type;
    } break;

    case ASTNODE_CALL:
    case ASTNODE_CALL_STATEMENT: {
        result_type = node_type(node->call.callee)->fn.ret_type;
    } break;

    case ASTNODE_BINOP: result_type = binop_type(node); break;

    case ASTNODE_NUMBER: result_type = primitive_type(PRIMITIVE_I32);    break;
    case ASTNODE_STRING: result_type = primitive_type(PRIMITIVE_STRING); break;
    case ASTNODE_BOOL:   result_type = primitive_type(PRIMITIVE_BOOL);   break;
    case ASTNODE_LIST_LITERAL: {
        TypeInfo *list_type = create_type_without_name(KIND_LIST);
        list_type->list.size = node->list.size;
        list_type->list.is_sized = node->list.size > 0;
        list_type->list.elts_type = node->list.elts_type;
        result_type = list_type;
    } break;

    case ASTNODE_IDENT: {
        Symbol *sym = get_symbol(node->name);
        if (!sym) {
            loc_print(node->loc);
            errorln("Identifier '%s' is not declared", node->name);
            exit(1);
        }
        node->ident = sym;
        result_type = sym->type;
    } break;

    default:
        unreachableln("Ast node type %u in node_type", node->type);
        exit(1);
    }

    if (!result_type) {
        loc_print(node->loc);
        unreachableln("Node type = %d", node->type);
        unreachableln("Type could not be determined or it's invalid in type check");
        exit(1);
    }

    node->resolved_type = result_type;
    return result_type;
}

// TODO: here I can do shortcircuit optimizations
TypeInfo *binop_type(ASTNode *node)
{
    TypeInfo *left  = node_type(node->binary.left);
    TypeInfo *right = node_type(node->binary.right);

    loc_print(node->loc);
    debugln("Operation: ('");
    type_print(left);
    printf("' %s '", operator_as_string(node->binary.op));
    type_print(right);
    printf("')\n");

    switch (node->binary.op) {
    case OP_PLUS:
        if (are_types_equal(left, right)) return left;
        break;
    case OP_STAR:
        if (are_types_equal(left, right)) return left;
        break;
    case OP_DOUBLE_EQUALS:
        if (are_types_equal(left, right)) return primitive_type(PRIMITIVE_BOOL);
        break;

    default:
        unreachableln("Operator %u in binop_type", node->binary.op);
        exit(1);
    }

    loc_print(node->loc);
    error("Invalid operation: ('");
    type_print(left);
    printf("' %s '", operator_as_string(node->binary.op));
    type_print(right);
    printf("')\n");
    exit(1);
}

void match_type(ASTNode *node, TypeInfo *type)
{
    TypeInfo *n_type = node_type(node);
    if (!are_types_equal_with_errors(type, n_type, NULL, NULL)) {
        loc_print(node->loc);
        error("Expected type '");
        type_print(type);
        printf("' but got '");
        type_print(n_type);
        printf("'\n");
        exit(1);
    }
}

void match_arguments(ASTNode *list, TypeInfo *type, Location fn_loc, Location call_loc)
{
    assert(type->kind == KIND_FUNCTION);

    size_t len = node_list_length(list);
    if (len != type->fn.params.count) {
        loc_print(call_loc);
        errorln("Mismatched number of arguments: wanted %zu, but got %zu", type->fn.params.count, len);
        loc_print(fn_loc);
        noteln("Function declared here");
        exit(1);
    }

    ASTNode *current = list;
    size_t i = 0;
    while (current) {
        TypeInfo *formal = type->fn.params.items[i];
        TypeInfo *actual = node_type(current);
        if (!are_types_equal_with_errors(formal, actual, NULL, NULL)) {
            loc_print(current->loc);
            error("Mismatched argument %zu: wanted ", i+1);
            type_print(formal);
            printf(" but got ");
            type_print(actual);
            printf("\n");
            exit(1);
        }
        current = current->next; 
        i++;
    }
}

static inline void register_symbol(Symbol *sym) { da_push(&da_get_last(scopes)->symbols, sym); }

void register_scope_types_and_functions(ASTNode *root)
{
    ASTNode *current = root;
    while (current) {
        if (current->type != ASTNODE_DECLARATION || current->decl.sym->kind == SYM_VARIABLE) {
            current = current->next;
            continue;
        }
        Symbol *sym = get_symbol(current->decl.sym->name);
        if (sym) {
            loc_print(current->loc);
            errorln("Redeclaration of name '%s'", current->decl.sym->name);
            loc_print(sym->loc);
            noteln("First declared here");
            exit(1);
        }
        register_symbol(current->decl.sym);
        current = current->next;
    }
}

static_assert(__astnode_types_count == 19, "Cover all ast node types in type_check");
void type_check(ASTNode *node)
{
    if (!node) return;

    switch (node->type) {
    case ASTNODE_PROGRAM: {
        register_scope_types_and_functions(node->next);
        Symbol *main_fn = get_function_symbol("main");
        if (!main_fn) {
            errorln("Entry point function 'main' is not declared");
            exit(1);
        } else {
            bool ok = false;
            for (int i = 0; i < MAIN_FN_ALTERNATIVES; i++) {
                if (are_types_equal(main_fn->type, _types_main_fn[i])) {
                    ok = true;
                    break;
                }
            }
            if (!ok) {
                loc_print(node->loc);
                errorln("Mismatched entry point function 'main' type declaration");
                note("Got ");
                type_print(main_fn->type);
                printf("\n");
                noteln("But expected one of these %u alternatives:", MAIN_FN_ALTERNATIVES);
                for (int i = 0; i < MAIN_FN_ALTERNATIVES; i++) {
                    note("%d: ", i+1);
                    type_print(_types_main_fn[i]);
                    printf("\n");
                }
                exit(1);
            }
        }
        type_check(node->next);
    } break;

    case ASTNODE_DECLARATION: {
        switch (node->decl.sym->kind) {
        case SYM_VARIABLE: {
            Symbol *other = get_symbol(node->decl.sym->name);
            if (other && !streq(other->name, "self")) { // TODO: is this correct?
                loc_print(node->loc);
                errorln("Redeclaration of name '%s'", node->decl.sym->name);
                loc_print(other->loc);
                noteln("First declared here");
                exit(1);
            }
            register_symbol(node->decl.sym);
            
            bool inferred = false;
            if (node->decl.sym->type->kind == KIND_NOT_INFERRED) {
                free(node->decl.sym->type);
                node->decl.sym->type = node_type(node->decl.var.init->assign.rhs);
                inferred = true;
            }
            if (node->decl.sym->type->kind == KIND_NOT_DECLARED) {
                node->decl.sym->type = get_type(node->decl.sym->type->name);
                if (node->decl.sym->type->kind == KIND_NOT_DECLARED) {
                    loc_print(node->loc);
                    errorln("Undeclared type '%s' for variable '%s'", node->decl.sym->type->name, node->name);
                    exit(1);
                }
            }

            if (!inferred && node->decl.var.init) {
                type_check(node->decl.var.init);
            }
        } break;
        case SYM_FUNCTION: {
            ASTNode *param_node = node->decl.fn.params;
            while (param_node) {
                type_check(param_node);
                param_node = param_node->next;
            }
            Scope fn_scope = {0};
            for (size_t i = 0; i < node->decl.sym->fn.params.count; i++) {
                Symbol *param = node->decl.sym->fn.params.items[i];
                node->decl.sym->type->fn.params.items[i] = param->type;
                da_push(&fn_scope.symbols, param);
            }

            TypeInfo *ret_type = node->decl.sym->type->fn.ret_type;
            if (ret_type->kind == KIND_NOT_DECLARED) {
                ret_type = get_type(ret_type->name); // TODO: is this correct?
                if (ret_type->kind == KIND_NOT_DECLARED) {
                    loc_print(node->loc);
                    errorln("Undeclared return type '%s' for function '%s'", ret_type->name, node->name);
                    exit(1);
                }
                node->decl.sym->type->fn.ret_type = ret_type;
            }

            push_scope(fn_scope); {
                type_check(node->decl.fn.block);
            } pop_scope();
        } break;
        case SYM_TYPE: {
            if (node->decl.sym->type->kind == KIND_STRUCT) {
                ASTNode *member_node = node->decl.strct.members;
                while (member_node) {
                    type_check(member_node);
                    member_node = member_node->next;
                }
                for (size_t i = 0; i < node->decl.sym->type->strct.members.count; i++) {
                    Symbol *member = node->decl.sym->type->strct.members.items[i];
                    if (member->type->kind == KIND_NOT_DECLARED) {
                        member->type = get_type(member->type->name);
                        if (member->type->kind == KIND_NOT_DECLARED) {
                            loc_print(node->loc);
                            errorln("Undeclared type '%s' for member '%s' (%zu) in struct '%s'",
                                    member->type->name, member->name, i+1, node->decl.sym->name);
                            exit(1);
                        }
                    }
                }
            } else {
                todoln("Type check %s declaration node", kind_as_string(node->decl.sym->type->kind));
                exit(1);
            }
        } break;
        default: unreachableln("SymbolKind in declaration node in type_check"); exit(1);
        }

        type_check(node->next);
    } break;

    case ASTNODE_ASSIGNMENT: {
        type_check(node->assign.lhs);
        TypeInfo *lhs_type = node_type(node->assign.lhs);

        if (lhs_type->kind == KIND_LIST) {
            ASTNode *list_node = node->assign.rhs;
            if (lhs_type->list.is_sized && lhs_type->list.size != list_node->list.size) {
                loc_print(node->loc);
                // TODO: create a type from the list_node and use are_types_equal_with_errors
                errorln("Assignment was expecting a list of size %zu, but got size %zu", lhs_type->list.size,
                        list_node->list.size);
                exit(1);
            }
            if (list_node->list.elts_type->kind == KIND_NOT_INFERRED) {
                list_node->list.elts_type = lhs_type->list.elts_type;
            }
            type_check(list_node); // populate list.elts_type
        }

        match_type(node->assign.rhs, lhs_type);
        type_check(node->next);
    } break;

    case ASTNODE_BLOCK: {
        push_new_scope(); {
            register_scope_types_and_functions(node->statements);
            type_check(node->statements);
        } pop_scope();
        type_check(node->next);
    } break;

    case ASTNODE_CALL:
    case ASTNODE_CALL_STATEMENT: {
        TypeInfo *callee_type = node_type(node->call.callee);
        if (callee_type->kind != KIND_FUNCTION) {
            loc_print(node->call.callee->loc);
            errorln("%s '%s' cannot be called", kind_as_string(callee_type->kind), callee_type->name);
            exit(1);
        }

        Symbol *fn = NULL;
        if (node->call.callee->type == ASTNODE_MEMBER_ACCESS) {
            ASTNode *instance = node->call.callee->accessed_struct;
            TypeInfo *struct_type = node_type(instance);
            assert(struct_type->kind == KIND_STRUCT);

            fn = get_struct_member(struct_type, node->call.callee->name);
            if (!fn) {
                loc_print(node->loc);
                errorln("Method '%s' is not declared on struct '%s'", node->call.callee->name, struct_type->name);
                exit(1);
            } else if (fn->kind != SYM_FUNCTION) {
                loc_print(node->loc);
                errorln("Member '%s' (%s) of struct '%s' is not callable", node->call.callee->name,
                        kind_as_string(fn->type->kind), struct_type->name);
                exit(1);
            }

            strcpy(node->name, fn->name);

            node->call.callee->type = ASTNODE_IDENT;
            strcpy(node->call.callee->name, fn->name);
            node->call.callee->ident = fn;

            if (!fn->type->fn.is_static) {
                ASTNode *self_arg = calloc(1, sizeof(ASTNode));
                *self_arg = *instance;
                self_arg->next = node->call.args;
                node->call.args = self_arg;
            }
        } else {
            fn = get_function_symbol(node->name); // TODO: check in all symbols, then make sure it's a function
            if (!fn) {
                loc_print(node->loc);
                errorln("Function '%s' is not declared", node->name);
                exit(1);
            }
        }

        match_arguments(node->call.args, fn->type, fn->loc, node->loc);

        if (node->type == ASTNODE_CALL_STATEMENT) {
            type_check(node->next);
        }
    } break;
    
    case ASTNODE_MEMBER_ACCESS: {
        type_check(node->accessed_struct);
        TypeInfo *struct_type = node_type(node->accessed_struct);

        if (struct_type->kind != KIND_STRUCT) {
            loc_print(node->loc);
            errorln("Cannot access member '%s' on %s", node->name, kind_as_string(struct_type->kind));
            exit(1);
        }

        Symbol *found = get_struct_member(struct_type, node->name);

        if (!found) {
            loc_print(node->loc);
            errorln("Struct '%s' does not have a member named '%s'", struct_type->name, node->name);
            exit(1);
        }
    } break;

    case ASTNODE_IF: {
        match_type(node->conditional.expr, primitive_type(PRIMITIVE_BOOL));
        type_check(node->conditional.block);
        if (node->conditional.list) type_check(node->conditional.list);
        type_check(node->next);
    } break;

    case ASTNODE_ELIF: {
        match_type(node->conditional.expr, primitive_type(PRIMITIVE_BOOL));
        type_check(node->conditional.block);
        if (node->next) type_check(node->next);
    } break;

    case ASTNODE_ELSE: {
        type_check(node->conditional.block);
    } break;

    case ASTNODE_WHILE: {
        match_type(node->conditional.expr, primitive_type(PRIMITIVE_BOOL));
        type_check(node->conditional.block);
        type_check(node->next);
    } break;

    case ASTNODE_FOR: {
        todoln("Type check node FOR");
        exit(1);
    } break;

    case ASTNODE_RETURN: {
        TypeInfo *expected_ret = node->ret.fn->decl.sym->type->fn.ret_type;
        if (!node->ret.expr && !are_types_equal(expected_ret, primitive_type(PRIMITIVE_VOID))) {
            loc_print(node->loc);
            error("Function '%s' expectes '", node->ret.fn->decl.sym->name);
            type_print(expected_ret);
            printf("' as return type\n");
            exit(1);
        } else match_type(node->ret.expr, expected_ret);
        if (node->next) {
            loc_print(node->next->loc);
            warningln("Unreachable code after return statement");
            type_check(node->next);
        }
    } break;

    case ASTNODE_BINOP: {
        type_check(node->binary.left);
        type_check(node->binary.right);
    } break;

    case ASTNODE_IDENT: {
        Symbol *sym = get_symbol(node->name);
        if (!sym) {
            loc_print(node->loc);
            errorln("Identifier '%s' is not declared", node->name);
            exit(1);
        }
        if (sym->type->kind == KIND_NOT_DECLARED) {
            Symbol *type_sym = get_type_symbol(sym->type->name);
            if (type_sym) {
                sym->type = type_sym->type;
            } else {
                loc_print(node->loc);
                errorln("Unknown type '%s' for identifier '%s'", sym->type->name, sym->name);
                exit(1);
            }
        }
        node->ident = sym;
    } break;

    case ASTNODE_NUMBER:
    case ASTNODE_STRING:
    case ASTNODE_BOOL:
        break;

    case ASTNODE_LIST_LITERAL: {
        if (node->list.size) {
            TypeInfo *elts_type = node_type(node->list.head);
            node->list.elts_type = elts_type;
        }
    } break;

    default:
        unreachableln("Ast node type %u in type_check", node->type);
        exit(1);
    }
}

void generate_fn_signature(Symbol *sym, FILE *f)
{
    if (sym->type->fn.ret_type->kind == KIND_LIST) {
        todoln("Return list from function");
        exit(1);
    }

    generate_type(sym->type->fn.ret_type, NULL, f);
    fprintf(f, " ");
    if (strlen(sym->prefix) > 0) fprintf(f, "%s_", sym->prefix);
    fprintf(f, "%s(", sym->name);
    if (da_is_empty(&sym->fn.params)) {
        fprintf(f, "void");
    } else {
        for (size_t i = 0; i < sym->fn.params.count; i++) {
            Symbol *param = sym->fn.params.items[i];
            if (param->type->kind == KIND_FUNCTION) {
                generate_fn_signature(param, f);
            } else {
                generate_type(param->type, param->name, f);
                fprintf(f, " %s", param->name);
            }
            if (i < sym->fn.params.count-1) fprintf(f, ", ");
        }
    }
    fprintf(f, ")");
}

static inline void generate_struct_type(Symbol *sym, FILE *f)
{
    fprintf(f, "typedef struct %s %s;\n", sym->name, sym->name);
}

void _generate_struct(Symbol *strct, FILE *f, bool with_type)
{
    if (with_type) fprintf(f, "typedef struct ");
    else fprintf(f, "struct %s ", strct->name);
    fprintf(f, "{\n");
    for (size_t i = 0; i < strct->type->strct.members.count; i++) {
        Symbol *member = strct->type->strct.members.items[i];
        if (member->kind != SYM_VARIABLE) continue;
        generate_type(member->type, member->name, f);
        fprintf(f, " %s;\n", member->name);
    }
    fprintf(f, "}");
    if (with_type) fprintf(f, " %s", strct->name);
    fprintf(f, ";\n");
}
static inline void generate_struct_with_type(Symbol *sym, FILE *f) { _generate_struct(sym, f, true); }
static inline void generate_struct(Symbol *sym, FILE *f) { _generate_struct(sym, f, false); }

void extract_declarations(ASTNode *node, Nodes *type_defs, Nodes *struct_defs, Nodes *fn_decls, Nodes *fn_defs, Nodes *glob_var_decls, Nodes *glob_var_defs)
{
    while (node) {
        switch (node->type) {
        case ASTNODE_DECLARATION:
            switch (node->decl.sym->kind) {
            case SYM_VARIABLE:
                if (!in_global_scope()) break;
                if (node->decl.sym->var.initialized) da_push(glob_var_defs, node);
                else da_push(glob_var_decls, node);
                break;
            case SYM_FUNCTION:
                da_push(fn_decls, node);
                da_push(fn_defs, node);
                extract_declarations(node->decl.fn.block, type_defs, struct_defs, fn_decls, fn_defs, glob_var_decls,
                        glob_var_defs);
                break;
            case SYM_TYPE:
                if (node->decl.sym->type->kind == KIND_STRUCT) {
                    da_push(type_defs, node);
                    da_push(struct_defs, node);

                    push_new_scope(); {
                        extract_declarations(node->decl.strct.members, type_defs, struct_defs, fn_decls, fn_defs, glob_var_decls, glob_var_defs);
                    } pop_scope();
                } else {
                    todoln("Extract type declaration from kind %s", kind_as_string(node->decl.sym->type->kind));
                    exit(1);
                }
                break;
            default: unreachableln("SymbolKind %d in extract_declarations", node->decl.sym->kind); exit(1);
            }
            break;
        case ASTNODE_BLOCK:
            push_new_scope(); {
                extract_declarations(node->statements, type_defs, struct_defs, fn_decls, fn_defs, glob_var_decls,
                        glob_var_defs);
            } pop_scope();
            break;
        case ASTNODE_IF:
        case ASTNODE_WHILE:
            extract_declarations(node->conditional.block, type_defs, struct_defs, fn_decls, fn_defs, glob_var_decls, glob_var_defs);
            if (node->type == ASTNODE_IF && node->conditional.list) {
                extract_declarations(node->conditional.list, type_defs, struct_defs, fn_decls, fn_defs, glob_var_decls, glob_var_defs);
            }
            break;
        case ASTNODE_ELIF:
            extract_declarations(node->conditional.block, type_defs, struct_defs, fn_decls, fn_defs, glob_var_decls, glob_var_defs);
            break;
        case ASTNODE_ELSE:
            extract_declarations(node->conditional.block, type_defs, struct_defs, fn_decls, fn_defs, glob_var_decls, glob_var_defs);
            break;
        default:
            break;
        }
        node = node->next;
    }
}

static_assert(__astnode_types_count == 19, "Cover all ast node types in generate_c_code");
void generate_c_code(ASTNode *node, FILE *f)
{
    if (!node) return;

    switch (node->type) {
    case ASTNODE_PROGRAM: {
        fprintf(f, "#include <string.h>\n");
        fprintf(f, "#include <stdint.h>\n\n");

        // TODO: no need for nodes I can just take the symbols
        Nodes type_defs = {0};
        Nodes struct_defs = {0};
        Nodes fn_decls = {0};
        Nodes fn_defs = {0};
        Nodes glob_var_decls = {0};
        Nodes glob_var_defs = {0};

        extract_declarations(node->next, &type_defs, &struct_defs, &fn_decls, &fn_defs, &glob_var_decls, &glob_var_defs);

        if (!da_is_empty(&glob_var_defs)) {
            errorln("Global variables cannot be initialized at the moment");
            for (size_t i = 0; i < glob_var_defs.count; i++) {
                Symbol *var = glob_var_defs.items[i]->decl.sym;
                loc_print(var->loc);
                note("'%s': ", var->name);
                type_print(var->type);
                printf("\n");
            }
            exit(1);
        }

        for (size_t i = 0; i < type_defs.count; i++) {
            generate_struct_type(type_defs.items[i]->decl.sym, f);
        }
        if (type_defs.count > 0) fprintf(f, "\n");

        for (size_t i = 0; i < fn_decls.count; i++) {
            generate_fn_signature(fn_decls.items[i]->decl.sym, f);
            fprintf(f, ";\n");
        }
        if (fn_decls.count > 0) fprintf(f, "\n");

        for (size_t i = 0; i < struct_defs.count; i++) {
            generate_struct(struct_defs.items[i]->decl.sym, f);
        }
        if (struct_defs.count > 0) fprintf(f, "\n");

        for (size_t i = 0; i < glob_var_decls.count; i++) {
            Symbol *var = glob_var_decls.items[i]->decl.sym;
            generate_type(var->type, var->name, f);
            fprintf(f, " %s;\n", var->name);
        }
        if (glob_var_decls.count > 0) fprintf(f, "\n");

        for (size_t i = 0; i < fn_defs.count; i++) {
            Symbol *sym = fn_defs.items[i]->decl.sym;
            generate_fn_signature(sym, f);
            fprintf(f, " ");
            push_new_scope(); {
                generate_c_code(fn_defs.items[i]->decl.fn.block, f);
            } pop_scope();
            fprintf(f, "\n");
        }

        da_free(&type_defs);
        da_free(&struct_defs);
        da_free(&fn_decls);
        da_free(&fn_defs);
        da_free(&glob_var_decls);
        da_free(&glob_var_defs);

        generate_c_code(node->next, f);
    } break;

    case ASTNODE_DECLARATION: {
        switch (node->decl.sym->kind) {
        case SYM_VARIABLE: {
            if (in_global_scope()) break; // global variables have been already been generated
            generate_type(node->decl.sym->type, node->decl.sym->name, f);
            fprintf(f, " ");
            if (node->decl.var.init) {
                generate_c_code(node->decl.var.init, f);
            } else {
                if (node->decl.sym->type->kind != KIND_LIST) {
                    fprintf(f, "%s", node->decl.sym->name);
                }
                fprintf(f, ";\n");
            }
        } break;
        case SYM_FUNCTION:
        case SYM_TYPE: 
            break;
        default: unreachableln("SymbolKind %d generating declaration node", node->decl.sym->kind); exit(1);
        }

        generate_c_code(node->next, f);
    } break;

    case ASTNODE_ASSIGNMENT: {
        TypeInfo *lhs_type = node_type(node->assign.lhs);

        if (lhs_type->kind == KIND_LIST && !node->assign.is_init) {
            fprintf(f, "memcpy(");
            generate_c_code(node->assign.lhs, f);
            fprintf(f, ", (");
            generate_type(lhs_type->list.elts_type, NULL, f);
            fprintf(f, "[])");
            generate_c_code(node->assign.rhs, f);
            fprintf(f, ", sizeof(");
            generate_c_code(node->assign.lhs, f);
            fprintf(f, "));\n");
        } else {
            generate_c_code(node->assign.lhs, f);
            fprintf(f, " = ");
            generate_c_code(node->assign.rhs, f);
            fprintf(f, ";\n");
        }
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_BLOCK: {
        fprintf(f, "{\n");

        push_new_scope(); {
            generate_c_code(node->statements, f);
        } pop_scope();

        fprintf(f, "}\n");
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_IF: {
        fprintf(f, "if (");
        generate_c_code(node->conditional.expr, f);
        fprintf(f, ") ");
        generate_c_code(node->conditional.block, f);
        if (node->conditional.list) generate_c_code(node->conditional.list, f);
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_ELIF: {
        fprintf(f, "else if (");
        generate_c_code(node->conditional.expr, f);
        fprintf(f, ") ");
        generate_c_code(node->conditional.block, f);
        if (node->next) generate_c_code(node->next, f);
    } break;

    case ASTNODE_ELSE: {
        fprintf(f, "else ");
        generate_c_code(node->conditional.block, f);
    } break;

    case ASTNODE_WHILE: {
        fprintf(f, "while (");
        generate_c_code(node->conditional.expr, f);
        fprintf(f, ") ");
        generate_c_code(node->conditional.block, f);
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_FOR: {
        todoln("Generate node FOR");
        exit(1);
        //fprintf(f, "for (");
        //generate_c_code(node->left, f);
        //generate_c_code(node->right, f);
        //fprintf(f, ") ");
        //generate_c_code(node->right, f);
        //generate_c_code(node->next, f);
    } break;

    case ASTNODE_RETURN: {
        fprintf(f, "return");
        if (node->ret.expr) {
            fprintf(f, " ");
            generate_c_code(node->ret.expr, f);
        }
        fprintf(f, ";\n");
        generate_c_code(node->next, f);
    } break;

    case ASTNODE_CALL:
    case ASTNODE_CALL_STATEMENT: {
        generate_c_code(node->call.callee, f);
        fprintf(f, "(");
        ASTNode *arg = node->call.args;
        while (arg) {
            generate_c_code(arg, f);
            if (arg->next) fprintf(f, ", ");
            arg = arg->next;
        }
        fprintf(f, ")");

        if (node->type == ASTNODE_CALL_STATEMENT) {
            fprintf(f, ";\n");
            generate_c_code(node->next, f);
        }
    } break;

    case ASTNODE_MEMBER_ACCESS: {
        generate_c_code(node->accessed_struct, f);
        fprintf(f, ".%s", node->name);
    } break;

    case ASTNODE_BINOP: {
        fprintf(f, "(");
        generate_c_code(node->binary.left, f);
        fprintf(f, ") %s (", operator_as_string(node->binary.op));
        generate_c_code(node->binary.right, f);
        fprintf(f, ")");
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

    case ASTNODE_LIST_LITERAL: {
        fprintf(f, "{");
        ASTNode *elt = node->list.head;
        while (elt) {
            generate_c_code(elt, f);
            if (elt->next) fprintf(f, ", ");
            elt = elt->next;
        }
        fprintf(f, "}");
    } break;

    case ASTNODE_IDENT: {
        if (strlen(node->ident->prefix) > 0) fprintf(f, "%s_", node->ident->prefix);
        fprintf(f, "%s", node->ident->name);
    } break;

    default:
        unreachableln("Ast node type %u in generate_c_code", node->type);
        exit(1);
    }
}

static_assert(__primitive_types_count == 5-1, "Instantiate all primitive types in initialize");
void initialize(void)
{
    _type_primitive_void   = (TypeInfo){ .kind = KIND_PRIMITIVE, .primitive = PRIMITIVE_VOID };
    _type_primitive_i32    = (TypeInfo){ .kind = KIND_PRIMITIVE, .primitive = PRIMITIVE_I32 };
    _type_primitive_string = (TypeInfo){ .kind = KIND_PRIMITIVE, .primitive = PRIMITIVE_STRING };
    _type_primitive_bool   = (TypeInfo){ .kind = KIND_PRIMITIVE, .primitive = PRIMITIVE_BOOL };

    TypeInfo *main_fn;

    main_fn = create_type_without_name(KIND_FUNCTION);
    main_fn->fn.ret_type = primitive_type(PRIMITIVE_VOID);
    _types_main_fn[0] = main_fn; // (void) -> void

    main_fn = create_type_without_name(KIND_FUNCTION);
    main_fn->fn.ret_type = primitive_type(PRIMITIVE_I32);
    _types_main_fn[1] = main_fn; // (void) -> int

    TypeInfo *main_args_type = create_type_without_name(KIND_LIST);
    main_args_type->list.elts_type = primitive_type(PRIMITIVE_STRING);

    main_fn = create_type_without_name(KIND_FUNCTION);
    main_fn->fn.ret_type = primitive_type(PRIMITIVE_VOID);
    da_push(&main_fn->fn.params, main_args_type);
    _types_main_fn[2] = main_fn; // ([string]) -> void

    main_fn = create_type_without_name(KIND_FUNCTION);
    main_fn->fn.ret_type = primitive_type(PRIMITIVE_I32);
    da_push(&main_fn->fn.params, main_args_type);
    _types_main_fn[3] = main_fn; // ([string]) -> int
}

int main(int argc, char **argv)
{
    Timer compilation_timer = {0};
    timer_start(&compilation_timer);

    char *program_name = shift_arg(&argc, &argv);

    if (argc < 1) {
        fprintf(stderr, "ERROR: file was not provided\n");
        usage(program_name);
        exit(1);
    }

    char *source_path = shift_arg(&argc, &argv);
    const char *dot = strrchr(source_path, '.');
    if (!dot || streq(source_path, dot) || !streq(KJUDE_FILE_EXTENSION, dot+1)) {
        errorln("Unrecognized file extension '%s', should have "KJUDE_FILE_EXTENSION, dot);
        exit(1);
    }

    bool flag_generate_only = false;
    bool flag_generate_and_finalize = false;
    if (argc >= 1) {
        char *flag = shift_arg(&argc, &argv);
             if (streq(flag, "-g")) flag_generate_only = true;
        else if (streq(flag, "-G")) flag_generate_and_finalize = true;
    }

    initialize();

    /// Begin Lexing
    Timer lexing_timer = {0};
    timer_start(&lexing_timer);

    Lexer lexer = lexer_new(source_path);
    Tokens tokens = lexer_lex(&lexer);

    timer_finish(&lexing_timer, "Lexing");
    /// End Lexing

    /// Begin Parsing
    Timer parsing_timer = {0};
    timer_start(&parsing_timer);

    Parser parser = parser_new(tokens);
    ASTNode *ast = parse_program(&parser);
    if (ast == NULL) {
        unreachableln("Compilation failed, ast is NULL");
        return 1;
    }

    timer_finish(&parsing_timer, "Parsing");
    /// End Parsing

    /// Begin Type checking
    Timer type_check_timer = {0};
    timer_start(&type_check_timer);

    type_check(ast);

    timer_finish(&type_check_timer, "Type checking");
    /// End Type checking

    /// Begin Generation
    Timer generation_timer = {0};
    timer_start(&generation_timer);

    char *c_filename = DEFAULT_KJUDE_INTERMEDIATE_C_FILE;
    FILE *c_file = fopen(c_filename, "w");
    if (c_file == NULL) {
        fprintf(stderr, "Could not open file `%s`\n", c_filename);
        exit(1);
    }
    generate_c_code(ast, c_file);
    fclose(c_file);

    timer_finish(&generation_timer, "Generation");

    if (flag_generate_only) goto end;
    /// End Generation

    /// Begin Finalization
    Timer finalization_timer = {0};
    timer_start(&finalization_timer);

    if (!compile_c()) return 1;

    if (!flag_generate_and_finalize)
        remove(c_filename);

    timer_finish(&finalization_timer, "Finalization");
    /// End Finalization


end:
    timer_finish(&compilation_timer, "Compilation overall");
    return 0;
}
