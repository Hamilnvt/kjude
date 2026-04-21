// TODO:
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
//     - method: first argument is 'this' (automatica type Struct) -> can be called as s.f() if s is a Struct and s is passed as first argument
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
// - Interesting conversation about future work: https://gemini.google.com/share/30240fe1ff3d
// - change "without" functions with "with the opposite"
// - type checking array literals means creating a list of types and checking if they all have the same type as the expected type
// - add Location to TypeInfo to report errors

// Grammar
//
// program -> (statement ;)*
// statement ->
//     declaration
//
// declaration ->
//     ident : type
//     ident : type = expr
//     ident :      = expr
// ...

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#define STRINGS_IMPLEMENTATION
#include "strings.h"

#define INDEX_NOT_FOUND ((size_t)-1)
#define DEFAULT_KJUDE_INTERMEDIATE_C_FILE "__kjude_intermediate.c"
#define KJUDE_RUNTIME_PATH "runtime/kjude_runtime.c"
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
#define KEYWORD_IN     "in"
#define KEYWORD_RETURN "return"
#define KEYWORD_STRUCT "struct"
#define KEYWORD_ANY    "Any"
#define KEYWORD_TYPE   "Type"
#define KEYWORD_TRUE   "true"
#define KEYWORD_FALSE  "false"
#define KEYWORD_EXTERN "extern"
#define KEYWORD_CAST   "cast"

typedef enum
{
    TOK_EOF,
    TOK_IDENT,
    TOK_IF,
    TOK_ELIF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_IN,
    TOK_RETURN,
    TOK_STRUCT,
    TOK_TRUE,
    TOK_FALSE,
    TOK_CAST,
    TOK_EXTERN,
    TOK_ANY,
    TOK_TYPE,
    TOK_NUMBER,
    TOK_STRING,
    TOK_L_PAREN,
    TOK_R_PAREN,
    TOK_L_SQPAREN,
    TOK_R_SQPAREN,
    TOK_L_CUPAREN,
    TOK_R_CUPAREN,
    TOK_COMMA,
    TOK_DOT,
    TOK_DOUBLE_DOT,
    TOK_HAT,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_EQUALS,
    TOK_LESS,
    TOK_GREATER,
    TOK_DOUBLE_EQUALS,
    TOK_NOT_EQUALS,
    TOK_LESS_EQUALS,
    TOK_GREATER_EQUALS,
    TOK_ARROW,
    TOK_PLUS,
    TOK_DOUBLE_PLUS,
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
    const char *lexeme;
    Location loc;
} Token;

static_assert(__tok_types_count == 41, "Cover all token types in token_type_as_string");
char *token_type_as_string(TokenType type)
{
    switch (type)
    {
        case TOK_EOF:           return "EOF";
        case TOK_IDENT:         return "Identifier";
        case TOK_IF:            return KEYWORD_IF;
        case TOK_ELIF:          return KEYWORD_ELIF;
        case TOK_ELSE:          return KEYWORD_ELSE;
        case TOK_WHILE:         return KEYWORD_WHILE;
        case TOK_FOR:           return KEYWORD_FOR;
        case TOK_IN:            return KEYWORD_IN;
        case TOK_RETURN:        return KEYWORD_RETURN;
        case TOK_STRUCT:        return KEYWORD_STRUCT;
        case TOK_ANY:           return KEYWORD_ANY;
        case TOK_TYPE:          return KEYWORD_TYPE;
        case TOK_TRUE:          return KEYWORD_TRUE;
        case TOK_FALSE:         return KEYWORD_FALSE;
        case TOK_EXTERN:        return KEYWORD_EXTERN;
        case TOK_CAST:          return KEYWORD_CAST;
        case TOK_NUMBER:        return "Number";
        case TOK_STRING:        return "String";
        case TOK_L_PAREN:       return "(";
        case TOK_R_PAREN:       return ")";
        case TOK_L_SQPAREN:     return "[";
        case TOK_R_SQPAREN:     return "]";
        case TOK_L_CUPAREN:     return "{";
        case TOK_R_CUPAREN:     return "}";
        case TOK_COMMA:         return ",";
        case TOK_DOT:           return ".";
        case TOK_DOUBLE_DOT:    return "..";
        case TOK_HAT:           return "^";
        case TOK_COLON:         return ":";
        case TOK_SEMICOLON:     return ";";
        case TOK_EQUALS:        return "=";
        case TOK_LESS:          return "<";
        case TOK_GREATER:       return ">";
        case TOK_DOUBLE_EQUALS: return "==";
        case TOK_NOT_EQUALS:    return "!=";
        case TOK_LESS_EQUALS:    return "<=";
        case TOK_GREATER_EQUALS: return ">=";
        case TOK_ARROW:         return "->";
        case TOK_PLUS:          return "+";
        case TOK_DOUBLE_PLUS:   return "++";
        case TOK_STAR:          return "*";
        default:
            unreachableln("Unknown token type %d in token_type_as_string", type);
            exit(1);
    }
}

static_assert(__tok_types_count == 41, "Cover all token types in token_print");
void token_print(const Token *tok)
{
    switch (tok->type) {
        case TOK_IDENT:
        case TOK_NUMBER:
        case TOK_STRING:
            printf("<%s, '%s'>", token_type_as_string(tok->type), tok->lexeme);
            break;

        case TOK_IF:
        case TOK_ELIF:
        case TOK_ELSE:
        case TOK_WHILE:
        case TOK_FOR:
        case TOK_IN:
        case TOK_RETURN:
        case TOK_STRUCT:
        case TOK_ANY:
        case TOK_TYPE:
        case TOK_TRUE:
        case TOK_FALSE:
        case TOK_EXTERN:
        case TOK_CAST:
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
        case TOK_DOUBLE_DOT:
        case TOK_HAT:
        case TOK_COLON:
        case TOK_SEMICOLON:
        case TOK_EQUALS:
        case TOK_LESS:
        case TOK_GREATER:
        case TOK_DOUBLE_EQUALS:
        case TOK_NOT_EQUALS:
        case TOK_LESS_EQUALS:
        case TOK_GREATER_EQUALS:
        case TOK_ARROW:
        case TOK_PLUS:
        case TOK_DOUBLE_PLUS:
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
    const char *source;
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

    lexer.loc = loc_new(file_path);
    return lexer;
}

static inline char lexer_current_char(const Lexer *lexer) { return lexer->source[lexer->pos]; }
#define current_char (lexer_current_char(lexer))
static inline void lexer_advance(Lexer *lexer)
{
    if (current_char == '\n') {
        lexer->loc.col = 0;
        lexer->loc.row++;
    } else {
        lexer->loc.col++;
    }
    lexer->pos++;
}

void lexer_eat_spaces(Lexer *lexer)
{
    while (isspace(current_char)) {
        lexer_advance(lexer);
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
static_assert(__tok_types_count == 41, "Cover all token types in lexer_get_next_token");
Token lexer_get_next_token(Lexer *lexer)
{
    lexer_eat_spaces(lexer);

    Token token = { .loc = lexer->loc, .type = TOK_INVALID };

    char c = current_char;
    String lexeme = {0};

    if (c == '\0') {
        token.type = TOK_EOF;
    } else if (can_start_ident(c)) {
        do {
            s_push(&lexeme, c);
            lexer->pos++;
            lexer->loc.col++;
            c = current_char;
        } while (can_continue_ident(c));
        s_push_null(&lexeme);
        
        // TODO: switch on length for perfomance
             if (streq(lexeme.items, KEYWORD_IF))     token.type = TOK_IF;
        else if (streq(lexeme.items, KEYWORD_ELIF))   token.type = TOK_ELIF;
        else if (streq(lexeme.items, KEYWORD_ELSE))   token.type = TOK_ELSE;
        else if (streq(lexeme.items, KEYWORD_WHILE))  token.type = TOK_WHILE;
        else if (streq(lexeme.items, KEYWORD_FOR))    token.type = TOK_FOR;
        else if (streq(lexeme.items, KEYWORD_IN))     token.type = TOK_IN;
        else if (streq(lexeme.items, KEYWORD_RETURN)) token.type = TOK_RETURN;
        else if (streq(lexeme.items, KEYWORD_STRUCT)) token.type = TOK_STRUCT;
        else if (streq(lexeme.items, KEYWORD_ANY))    token.type = TOK_ANY;
        else if (streq(lexeme.items, KEYWORD_TYPE))   token.type = TOK_TYPE;
        else if (streq(lexeme.items, KEYWORD_TRUE))   token.type = TOK_TRUE;
        else if (streq(lexeme.items, KEYWORD_FALSE))  token.type = TOK_FALSE;
        else if (streq(lexeme.items, KEYWORD_EXTERN)) token.type = TOK_EXTERN;
        else if (streq(lexeme.items, KEYWORD_CAST))   token.type = TOK_CAST;
        else                                          token.type = TOK_IDENT;
    } else if (can_start_number(c)) {
        do {
            s_push(&lexeme, c);
            lexer->pos++;
            lexer->loc.col++;
            c = current_char;
        } while (can_continue_number(c));
        s_push_null(&lexeme);
        token.type = TOK_NUMBER;
    } else {
        s_push(&lexeme, c);
        lexer->pos++;
        lexer->loc.col++;

        switch (c) {
            case ',': token.type = TOK_COMMA;     break;
            case '.': {
                if (current_char == '.') {
                    s_push(&lexeme, current_char);
                    lexer->pos++;
                    lexer->loc.col++;
                    token.type = TOK_DOUBLE_DOT;
                } else token.type = TOK_DOT;
            } break;
            case '^': token.type = TOK_HAT;       break;
            case '!': {
                if (current_char == '=') {
                    s_push(&lexeme, current_char);
                    lexer->pos++;
                    lexer->loc.col++;
                    token.type = TOK_NOT_EQUALS;
                }
            } break;
            case ':': token.type = TOK_COLON;     break;
            case ';': token.type = TOK_SEMICOLON; break;
            case '<': {
                if (current_char == '=') {
                    s_push(&lexeme, current_char);
                    lexer->pos++;
                    lexer->loc.col++;
                    token.type = TOK_LESS_EQUALS;
                } else token.type = TOK_LESS;
            } break;
            case '>': {
                if (current_char == '=') {
                    s_push(&lexeme, current_char);
                    lexer->pos++;
                    lexer->loc.col++;
                    token.type = TOK_GREATER_EQUALS;
                } else token.type = TOK_GREATER;
            } break;
            case '=': {
                if (current_char == '=') {
                    token.type = TOK_DOUBLE_EQUALS;
                    s_push(&lexeme, current_char);
                    lexer->pos++;
                    lexer->loc.col++;
                } else {
                    token.type = TOK_EQUALS;
                }
            } break;
            case '+': {
                if (current_char == '+') {
                    token.type = TOK_DOUBLE_PLUS;
                    s_push(&lexeme, current_char);
                    lexer->pos++;
                    lexer->loc.col++;
                } else {
                    token.type = TOK_PLUS;
                }
            } break;
            case '-': {
                if (current_char == '>') {
                    token.type = TOK_ARROW;
                    s_push(&lexeme, current_char);
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
                s_clear(&lexeme);
                c = current_char;
                while (c != '\0' && c != '"') {
                    s_push(&lexeme, c);
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

        s_push_null(&lexeme);
    }

    if (token.type == TOK_INVALID) {
      loc_print(token.loc);
      errorln("Invalid token starting with '%c'", c);
      exit(1);
    }

    token.lexeme = lexeme.items;

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
    ASTNODE_UNARY,
    ASTNODE_BINOP,
    ASTNODE_TYPEOF,
    ASTNODE_CAST,

    ASTNODE_IDENT,
    ASTNODE_NUMBER,
    ASTNODE_STRING,
    ASTNODE_BOOL,
    ASTNODE_ARRAY_LITERAL,
    ASTNODE_ARRAY_INTERVAL,
    ASTNODE_ARRAY_ACCESS,

    __astnode_types_count
} ASTNodeType;

static_assert(__astnode_types_count == 24, "Cover all ast node types in astnode_type_as_string");
const char *astnode_type_as_string(ASTNodeType type)
{
    switch (type) {
    case ASTNODE_PROGRAM:        return "Program";
    case ASTNODE_DECLARATION:    return "Declaration";
    case ASTNODE_ASSIGNMENT:     return "Assignment";
    case ASTNODE_BLOCK:          return "Block";
    case ASTNODE_IF:             return "If";
    case ASTNODE_ELIF:           return "Elif";
    case ASTNODE_ELSE:           return "Else";
    case ASTNODE_WHILE:          return "While";
    case ASTNODE_FOR:            return "For";
    case ASTNODE_RETURN:         return "Return";
    case ASTNODE_CALL:           return "Call";
    case ASTNODE_CALL_STATEMENT: return "Call_statement";
    case ASTNODE_MEMBER_ACCESS:  return "Member_access";
    case ASTNODE_UNARY:          return "Unary";
    case ASTNODE_BINOP:          return "Binop";
    case ASTNODE_TYPEOF:         return "Typeof";
    case ASTNODE_CAST:           return "Cast";
    case ASTNODE_IDENT:          return "Ident";
    case ASTNODE_NUMBER:         return "Number";
    case ASTNODE_STRING:         return "String";
    case ASTNODE_BOOL:           return "Bool";
    case ASTNODE_ARRAY_LITERAL:  return "Array literal";
    case ASTNODE_ARRAY_INTERVAL: return "Array interval";
    case ASTNODE_ARRAY_ACCESS:   return "Array access";
    default:
        unreachableln("Node type %d in astnode_type_as_string", type);
        exit(1);
    }
}

typedef enum
{
    // Unary
    OP_POINTER_TO,

    OP_DEREFERENCE,

    // Binary
    OP_PLUS,
    OP_STAR,

    OP_DOUBLE_EQUALS,
    OP_NOT_EQUALS,
    //OP_LESS,
    //OP_GREATER,
    OP_LESS_EQUALS,
    OP_GREATER_EQUALS,

    __op_types_count,
} Operator;

static_assert(__op_types_count == 8, "Cover all operators in operator_from_token_type");
Operator operator_from_token_type(TokenType type)
{
    switch (type) {
    case TOK_HAT:           return OP_POINTER_TO;
    case TOK_DOT:           return OP_DEREFERENCE;
    case TOK_PLUS:          return OP_PLUS;
    case TOK_STAR:          return OP_STAR;
    case TOK_DOUBLE_EQUALS: return OP_DOUBLE_EQUALS;
    case TOK_NOT_EQUALS:    return OP_NOT_EQUALS;
    case TOK_LESS_EQUALS:   return OP_LESS_EQUALS;
    case TOK_GREATER_EQUALS: return OP_GREATER_EQUALS;
    default:
        unreachableln("Token '%s' in operator_from_token_type", token_type_as_string(type));
        exit(1);
    }
}

static_assert(__op_types_count == 8, "Cover all operators in operator_as_string");
char *operator_as_string(Operator op)
{
    switch (op) {
    case OP_POINTER_TO:    return "^";
    case OP_DEREFERENCE:   return ".";

    case OP_PLUS:          return "+";
    case OP_STAR:          return "*";
    case OP_DOUBLE_EQUALS: return "==";
    case OP_NOT_EQUALS:    return "!=";
    case OP_LESS_EQUALS:   return "<=";
    case OP_GREATER_EQUALS: return ">=";
    default:
        unreachableln("Operator %u in operator_as_string", op);
        exit(1);
    }
}

typedef enum
{
    KIND_INVALID = 0,

    KIND_TYPE,
    KIND_ANY,
    KIND_GENERIC,
    KIND_PRIMITIVE,
    KIND_STRUCT,
    KIND_FUNCTION,
    KIND_POINTER,
    KIND_ARRAY,

    __kinds_count
} Kind;

static_assert(__kinds_count == 9, "Cover all kinds in kind_as_string");
char *kind_as_string(Kind kind)
{
    switch (kind) {
    case KIND_INVALID: return "Invalid";
    case KIND_TYPE:         return "Type";
    case KIND_ANY:          return "Any";
    case KIND_GENERIC:      return "Generic Parameter";
    case KIND_PRIMITIVE:    return "Primitive";
    case KIND_STRUCT:       return "Struct";
    case KIND_FUNCTION:     return "Function";
    case KIND_POINTER:      return "Pointer";
    case KIND_ARRAY:        return "Array";
    default:
        unreachableln("Kind %u in kind_as_string", kind);
        exit(1);
    }
}

typedef enum
{
    PRIMITIVE_UNKNOWN = -1,

    PRIMITIVE_VOID = 0,
    PRIMITIVE_INT,
    //PRIMITIVE_UINT,
    PRIMITIVE_STRING,
    PRIMITIVE_BOOL,

    __primitive_types_count
} PrimitiveType;

static_assert(__primitive_types_count == 5-1, "Cover all primitive types in primitive_type_as_string");
char *primitive_type_as_string(PrimitiveType type)
{
    switch (type) {
    case PRIMITIVE_VOID:   return "void";
    case PRIMITIVE_INT:    return "int";
    //case PRIMITIVE_UINT:    return "uint";
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
    case PRIMITIVE_INT:    return "int32_t";
    //case PRIMITIVE_UINT:   return "uint32_t";
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

static_assert(__kinds_count == 9, "Cover all kinds in TypeInfo struct");
typedef struct TypeInfo
{
    Kind kind;
    const char *name;
    union {
        TypeInfo *type_value; // TYPE
        PrimitiveType primitive; // PRIMITIVE
        struct {
            TypeInfos params;
            struct TypeInfo *ret_type;
            bool is_method; // struct function
            bool is_static; // struct function without 'this'

            bool is_external;
            const char *header;
            const char *alias;
        } fn; // FUNCTION
        struct {
            TypeInfo *of;
            size_t size;
        } array; // ARRAY
        struct {
            Symbols members;
            bool is_generic;
            TypeInfos params;

            const char *prefix;
            uint64_t scope_id;
        } strct; // STRUCT
        struct {
            TypeInfo *to;
        } ptr; // POINTER
    };
} TypeInfo;

void type_print(TypeInfo *type);
static void generate_symbol_name(Symbol *sym, FILE *f);

static_assert(__kinds_count == 9, "Cover all kinds in generate_type");
void generate_type(TypeInfo *type, const char *name, FILE *f, Symbol *sym)
{
    switch (type->kind) {
    case KIND_INVALID:
    case KIND_GENERIC:
        unreachableln("Type checking should have resolved KIND_INVALID and KIND_GENERIC");
        exit(1);

    case KIND_TYPE:
        generate_type(type->type_value, NULL, f, NULL);
        break;

    case KIND_ANY:
        todoln("Generate kind %s", kind_as_string(KIND_ANY));
        exit(1);
        break;

    case KIND_PRIMITIVE:
        fprintf(f, "%s", primitive_type_to_c(type->primitive));
        if (name) fprintf(f, " %s", name);
        break;

    case KIND_ARRAY: {
        generate_type(type->array.of, NULL, f, NULL);
        if (name) {
            fprintf(f, " %s[%zu]", name, type->array.size);
        } else {
            fprintf(f, "[%zu]", type->array.size);
        }
    } break;

    case KIND_FUNCTION: {
        todoln("Write function to c");
        exit(1);
    } break;

    case KIND_STRUCT: {
        if (type->strct.scope_id != 0) {
            if (type->strct.prefix) fprintf(f, "%s_", type->strct.prefix);
            fprintf(f, "%s", type->name);
            fprintf(f, "_%lu", type->strct.scope_id);
        } else {
            fprintf(f, "%s", type->name);
        }
        if (name) fprintf(f, " %s", name);
        break;
    } break;

    case KIND_POINTER: {
        generate_type(type->ptr.to, NULL, f, sym);
        fprintf(f, "*");
        if (name) fprintf(f, " %s", name);
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

    //debug("Comparing types: '");
    //type_print(a);
    //printf("' and '");
    //type_print(b);
    //printf("'\n");

    if (a == NULL || b == NULL) {
        if (a == NULL && loc_a) loc_print(*loc_a);
        else if (b == NULL && loc_b) loc_print(*loc_b);
        unreachableln("Type is NULL in _are_types_equal");
        exit(1);
    }

    if (a == b) return true;

    if (a->kind != b->kind) {
        if (report_errors) {
            errorln("Types have different kinds '%s' and '%s'", kind_as_string(a->kind), kind_as_string(b->kind));
        }
        return false;
    }

    static_assert(__kinds_count == 9, "Cover all kinds in _are_types_equal");
    switch (a->kind) {

    case KIND_INVALID:
    case KIND_ANY:
        return true;

    case KIND_GENERIC: return(streq(a->name, b->name));

    case KIND_TYPE: return _are_types_equal(a->type_value, b->type_value, report_errors, loc_a, loc_b);

    case KIND_PRIMITIVE: return a->primitive == b->primitive;

    case KIND_ARRAY: {
        if (!_are_types_equal(a->array.of, b->array.of, report_errors, loc_a, loc_b)) {
            if (report_errors) {
                error("Arrays have different elements types '");
                type_print(a->array.of);
                printf("' and '");
                type_print(b->array.of);
                printf("'\n");
            }
            return false;
        }
        if (a->array.size != b->array.size) {
            if (report_errors) {
                errorln("Arrays have different size: %zu and %zu", a->array.size, b->array.size);
            }
            return false;
        }
        return true;
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
                    type_print(pb);
                    printf("'\n");
                }
                return false;
            }
        }
        return true;
    } break;
    case KIND_STRUCT: {
        if (!streq(a->name, b->name)) {
            if (report_errors) {
                errorln("Structs have different names '%s' and '%s'", a->name, b->name);
            }
            return false;
        }
        return true;
    } break;

    case KIND_POINTER: return _are_types_equal(a->ptr.to, b->ptr.to, report_errors, loc_a, loc_b);

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

static_assert(__kinds_count == 9, "Cover all kinds in type_print");
void type_print(TypeInfo *type)
{
    switch (type->kind) {
    case KIND_INVALID:
    case KIND_ANY:
    case KIND_TYPE:
        printf("%s", kind_as_string(type->kind));
        break;

    case KIND_GENERIC: printf("%s", type->name); break;

    case KIND_PRIMITIVE: printf("%s", primitive_type_as_string(type->primitive)); break;

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

    case KIND_ARRAY:
        printf("[");
        type_print(type->array.of);
        if (type->array.size > 0)
            printf(", %zu", type->array.size);
        printf("]");
        break;

    case KIND_POINTER:
        printf("^");
        type_print(type->ptr.to);
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
    if (name) type->name = name;
    return type;
}
static inline  TypeInfo *create_type_without_name(Kind kind) { return create_type(kind, NULL); }

static_assert(__primitive_types_count == 5-1, "Cover all primitive types in PrimitiveValue union");
typedef union
{
    const char *string;
    bool _bool;
    //uint32_t _uint;
    int32_t _int;
} PrimitiveValue;

static uint64_t scope_id_counter = 1;
typedef struct Scope
{
    bool is_global;
    uint64_t id;
    const char *prefix;
    Symbols symbols;
    struct Scope *outer;
} Scope;

typedef struct
{
    Scope *current;
    Scope **items;
    size_t count;
    size_t capacity;
} Scopes;
static Scopes scopes = {0};
static void push_new_scope_with_prefix(const char *prefix)
{
    Scope *enclosing = scopes.current;
    Scope *new_scope = calloc(1, sizeof(Scope));
    new_scope->id = scope_id_counter++;
    new_scope->outer = enclosing;
    new_scope->prefix = prefix;
    new_scope->is_global = false;
    da_push(&scopes, new_scope);
    scopes.count++;
    scopes.current = new_scope;
}
static inline void push_new_scope(void) { push_new_scope_with_prefix(NULL); }
static inline Scope *pop_scope(void)
{
    if (!scopes.current) return NULL;
    Scope *s = scopes.current;
    scopes.current = scopes.current->outer;
    if (!scopes.current && s->is_global) scopes.current = s;
    return s;
}

typedef enum
{
    SYM_VARIABLE,
    SYM_FUNCTION,
    SYM_STRUCT,

    __symbol_kinds_count
} SymbolKind;
typedef SymbolKind DeclarationKind;

struct Symbol
{
    SymbolKind kind;
    TypeInfo *type;
    Location loc;
    const char *name;
    Scope *scope;
    union {
        struct { // SYM_VARIABLE
            bool initialized;
        } var;
        struct { // SYM_FUNCTION
            Symbols params;
            bool is_external;
        } fn;
        struct { // SYM_STRUCT
            Symbols members;
            bool is_generic;
            Symbols params;
        } strct;
    };
};

typedef Symbols FunctionStack;
static FunctionStack function_stack = {0};
static inline void push_current_function(Symbol *fn) { da_push(&function_stack, fn); }
static inline Symbol *pop_current_function(void) { return da_pop(&function_stack); }
static inline Symbol *get_current_function(void)
{
    return function_stack.count > 0 ? *da_get_last(function_stack) : NULL;
}

void dump_symbols(void)
{
    debugln("Dumping symbols:");
    for (size_t scope_i = 0; scope_i < scopes.count; scope_i++) {
        Scope *scope = scopes.items[scope_i];
        debugln("%lu:", scope->id);
        for (size_t sym_i = 0; sym_i < scope->symbols.count; sym_i++) {
            debugln("- %s", scope->symbols.items[sym_i]->name);
        }
    }
}

static TypeInfo _type_type;
static inline TypeInfo *type_type(void) { return &_type_type; }
static TypeInfo _type_any;
static inline TypeInfo *any_type(void) { return &_type_any; }

static_assert(__primitive_types_count == 5-1, "Declare all primitive types and retrieve them in primitive_type");
static TypeInfo _type_primitive_void;
static TypeInfo _type_primitive_int;
//static TypeInfo _type_primitive_uint;
static TypeInfo _type_primitive_string;
static TypeInfo _type_primitive_bool;
static inline TypeInfo *primitive_type(PrimitiveType type)
{
    switch (type) {
    case PRIMITIVE_VOID:   return &_type_primitive_void;
    case PRIMITIVE_INT:    return &_type_primitive_int;
    //case PRIMITIVE_UINT:    return &_type_primitive_uint;
    case PRIMITIVE_STRING: return &_type_primitive_string;
    case PRIMITIVE_BOOL:   return &_type_primitive_bool;
    default:
        unreachableln("Primitive type %u in primitive_type", type);
        exit(1);
    }
}

#define MAIN_FN_ALTERNATIVES (4)
static TypeInfo *_types_main_fn[MAIN_FN_ALTERNATIVES] = {0};

Symbol *get_symbol(const char *name)
{
    for (Scope *scope = scopes.current; scope; scope = scope->outer) {
        for (size_t sym_i = 0; sym_i < scope->symbols.count; sym_i++) {
            Symbol *sym = scope->symbols.items[sym_i];
            if (streq(name, sym->name)) return sym;
        }
    }
    return NULL;
    return NULL;
}

static Symbol *get_local_symbol(const char *name)
{
    if (!scopes.current) return NULL;
    for (size_t sym_i = 0; sym_i < scopes.current->symbols.count; sym_i++) {
        Symbol *sym = scopes.current->symbols.items[sym_i];
        if (streq(name, sym->name)) return sym;
    }
    return NULL;
}

typedef struct ASTNode
{
    ASTNodeType type;
    Location loc;
    const char *name;

    bool type_checked; // TODO: is this useful ? 
    TypeInfo *resolved_type;

    static_assert(__astnode_types_count == 24, "Cover all ast node types ASTNode struct");
    union {
        Symbol *ident; // IDENT
        PrimitiveValue value; // NUMBER, STRING, BOOL
        struct {
            Operator op;
            ASTNode *left;
            ASTNode *right;
        } binary; // BINOP
        struct {
            Operator operator;
            ASTNode *operand;
        } unary; // UNARY
        struct {
            ASTNode *expr;
            ASTNode *block;
            ASTNode *list; // elif list or else (for IF and ELIS)
        } conditional; // IF, ELIF, WHILE
        ASTNode *statements; // BLOCK
        struct {
            ASTNode *of;
            ASTNode *size;
            ASTNode *head;
        } array; // ARRAY_LITERAL
        struct {
            ASTNode *begin;
            ASTNode *end;
            ASTNode *step;
            bool end_exclusive;
        } interval; // ARRAY_INTERVAL
        struct {
            ASTNode *array;
            ASTNode *index;
        } array_access; // ARRAY_ACCESS
        struct {
            ASTNode *expr;
        } ret; // RETURN
        ASTNode *accessed_struct; //MEMBER_ACCESS,
        struct {
            ASTNode *callee;
            ASTNode *args;
        } call; // CALL, CALL_STATEMENT
        struct {
            ASTNode *expr;
        } type_of;
        struct {
            ASTNode *expr;
            ASTNode *to;
        } cast; // CAST
        struct {
            ASTNode *lhs;
            ASTNode *rhs;
            bool is_init;
        } assign; // ASSIGNMENT
        struct {
            DeclarationKind kind;
            Symbol *sym;
            union {
                struct {
                    ASTNode *type;
                    ASTNode *init; // ASSIGNMENT node where lhs is DECLARATION node and rhs is expr
                } var;
                struct {
                    ASTNode *params;
                    ASTNode *block;
                    ASTNode *ret;
                    bool is_method;
                    bool is_static_method;

                    bool is_external;
                    const char *header;
                    const char *alias;
                } fn;
                struct {
                    ASTNode *members;
                    bool is_generic;
                    ASTNode *params;
                } strct;
            };
        } decl; // DECLARATION
        struct {
            ASTNode *value;
            bool value_by_pointer;
            ASTNode *index;
            ASTNode *list;
            ASTNode *block;
        } iterative;
    };
    ASTNode *next; // all statements, lists
} ASTNode;

ASTNode *create_node(ASTNodeType type, const char *name, Location loc)
{
    ASTNode *node = calloc(1, sizeof(ASTNode));
    assert(node);
    
    node->type = type;
    node->loc = loc;
    if (name) node->name = name;
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
    PREC_UNARY  = 40, // ^
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
    node->value._int = atoi(current_token->lexeme); // TODO: use strtol
    parser_advance(parser);
    return node;
}

ASTNode *parse_string(Parser *parser)
{
    ASTNode *node = create_node_from_token(ASTNODE_STRING, current_token);
    node->value.string = current_token->lexeme;
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
//        case TYPE_UINT: return sizeof(int32_t);
//        default: 
//            errorln("Unreachable type %u in size_of_type", type);
//            exit(1);
//    }
//}

static_assert(__primitive_types_count == 5-1, "Cover all primitive types in primitive_type_from_string");
PrimitiveType primitive_type_from_string(const char *type_string)
{
         if (streq(type_string, primitive_type_as_string(PRIMITIVE_VOID)))   return PRIMITIVE_VOID;
    else if (streq(type_string, primitive_type_as_string(PRIMITIVE_INT)))    return PRIMITIVE_INT;
    //else if (streq(type_string, primitive_type_as_string(PRIMITIVE_UINT)))   return PRIMITIVE_UINT;
    else if (streq(type_string, primitive_type_as_string(PRIMITIVE_STRING))) return PRIMITIVE_STRING;
    else if (streq(type_string, primitive_type_as_string(PRIMITIVE_BOOL)))   return PRIMITIVE_BOOL;
    else                                                                     return PRIMITIVE_UNKNOWN;
}

ASTNode *parse_statement(Parser *parser);

ASTNode *parse_block(Parser *parser)
{
    parser_expect(parser, TOK_L_CUPAREN); // '{'

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

    return node;
}

static inline ASTNode *parse_declaration(Parser *parser);
void _parse_function_declaration(Parser *parser, ASTNode *node, const char *prefix)
{
    parser_expect(parser, TOK_L_PAREN);

    ASTNode **next_param = &node->decl.fn.params;

    bool done = false;
    if (prefix) {
        node->decl.fn.is_method = true;
        if (streq(current_token->lexeme, "this")) {
            ASTNode *this_node = create_node_from_token(ASTNODE_DECLARATION, current_token);
            *next_param = this_node;
            next_param = &this_node->next;

            parser_advance(parser); // "this"
            if (current_token->type == TOK_R_PAREN) done = true;
            else parser_expect(parser, TOK_COMMA);
        } else {
            node->decl.fn.is_static_method = true;
        }
    }

    while (current_token->type != TOK_R_PAREN && !done) {
        ASTNode *param = parse_declaration(parser);
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

    parser_expect(parser, TOK_R_PAREN);

    if (current_token->type == TOK_ARROW) {
        parser_advance(parser); // "->"
        node->decl.fn.ret = parse_expression(parser);
    }

    parser_expect(parser, TOK_EQUALS);

    if (current_token->type == TOK_EXTERN) {
        parser_advance(parser); // "extern"
        node->decl.fn.is_external = true;
        parser_expect(parser, TOK_SEMICOLON);
    } else {
        // TODO: maybe if current != { we can parse a single statement, like in if, but at least here we got = to divide
        // TODO: functions with a single return statement (params) => expr (can be inferred) (used also for lambdas)
        node->decl.fn.block = parse_block(parser);
    }
}

static inline ASTNode *parse_declaration_statement_with_prefix(Parser *parser, const char *prefix);
void _parse_struct_declaration(Parser *parser, ASTNode *node)
{
    parser_advance(parser); // "struct"

    if (current_token->type == TOK_LESS) { // struct is generic
        parser_advance(parser); // "<"
        node->decl.strct.is_generic = true;

        ASTNode **next_param = &node->decl.strct.params;

        while (current_token->type != TOK_GREATER) {
            ASTNode *param = parse_declaration(parser);

            *next_param = param;
            next_param = &param->next;
            
            if (current_token->type == TOK_COMMA) parser_advance(parser); // ","
            else if (current_token->type != TOK_GREATER) {
                loc_print(current_token->loc);
                error("Expected ',' or '>' in generic parameter list, but got ");
                token_print(current_token);
                printf("\n");
                exit(1);
            }
        }
        parser_advance(parser); // ">"
    }

    parser_expect(parser, TOK_EQUALS);

    parser_expect(parser, TOK_L_CUPAREN);

    ASTNode **next_member = &node->decl.strct.members;
    while (current_token->type != TOK_R_CUPAREN) {
        ASTNode *member = parse_declaration_statement_with_prefix(parser, node->name);
        *next_member = member;
        next_member = &member->next;
    }
    parser_expect(parser, TOK_R_CUPAREN);
}

void _parse_variable_declaration(Parser *parser, ASTNode *node)
{
    if (current_token->type != TOK_EQUALS) {
        node->decl.var.type = parse_expression(parser);
        if (!node->decl.var.type) {
            loc_print(current_token->loc);
            error("Expecting type expression in variable declaration, but got ");
            token_print(current_token);
            printf("\n");
            exit(1);
        }
    }

    if (current_token->type == TOK_EQUALS) {
        parser_advance(parser); // "="
        ASTNode *var_init_expr = parse_expression(parser);
        if (!var_init_expr) {
            loc_print(current_token->loc);
            error("Expected expression in variable initialization, but got ");
            token_print(current_token);
            printf("\n");
            exit(1);
        }
        ASTNode *lhs = create_node(ASTNODE_IDENT, node->name, node->loc);
        ASTNode *assign = create_node_without_name(ASTNODE_ASSIGNMENT, var_init_expr->loc);
        assign->assign.lhs = lhs;
        assign->assign.rhs = var_init_expr;
        assign->assign.is_init = true;
        node->decl.var.init = assign;
    }
}

ASTNode *_parse_declaration(Parser *parser, bool is_statement, const char *prefix)
{
    ASTNode *node = create_node_from_token(ASTNODE_DECLARATION, current_token);

    parser_advance(parser); // name
    parser_expect(parser, TOK_COLON);

    if (current_token->type == TOK_STRUCT) {
        node->decl.kind = SYM_STRUCT;
        _parse_struct_declaration(parser, node);
    } else if (current_token->type == TOK_L_PAREN) {
        node->decl.kind = SYM_FUNCTION;
        _parse_function_declaration(parser, node, prefix);
    } else {
        node->decl.kind = SYM_VARIABLE;
        _parse_variable_declaration(parser, node);
        if (is_statement) parser_expect(parser, TOK_SEMICOLON);
    }

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

ASTNode *parse_ident(Parser *parser)
{
    ASTNode *node = create_node_from_token(ASTNODE_IDENT, current_token);
    parser_advance(parser); // ident name
    return node;
}

ASTNode *parse_for(Parser *parser)
{
    parser_expect(parser, TOK_FOR);

    ASTNode *node = create_node_from_token(ASTNODE_FOR, current_token);

    if (current_token->type == TOK_HAT) {
        parser_advance(parser); // "^"
        node->iterative.value_by_pointer = true;
    }

    ASTNode *value = create_node_from_token(ASTNODE_DECLARATION, current_token);
    value->decl.kind = SYM_VARIABLE;

    parser_expect(parser, TOK_IDENT); // TODO: write error

    if (current_token->type == TOK_COMMA) {
        parser_advance(parser); // "^"

        ASTNode *index = create_node_from_token(ASTNODE_DECLARATION, current_token);
        index->decl.kind = SYM_VARIABLE;
        index->decl.var.type = create_node(ASTNODE_IDENT, "int", current_token->loc); // TODO: it will be uint
        index->decl.var.init = create_node_without_name(ASTNODE_NUMBER, current_token->loc);
        index->decl.var.init->value._int = 0; // TODO: uint
        node->iterative.index = index;
        parser_expect(parser, TOK_IDENT); // TODO: write error
    }

    parser_expect(parser, TOK_IN);

    node->iterative.list = parse_expression(parser);
    if (!node->iterative.list) {
        error("Expecting iterable in for statement, but got '");
        token_print(current_token);
        printf("'\n");
        exit(1);
    }

    ASTNode *first_element = create_node_without_name(ASTNODE_ARRAY_ACCESS, value->loc);
    first_element->array_access.array = node->iterative.list;
    first_element->array_access.index = create_node_without_name(ASTNODE_NUMBER, current_token->loc);
    first_element->array_access.index->value._int = 0; // TODO: uint

    ASTNode *type_of_first_element = create_node_without_name(ASTNODE_TYPEOF, value->loc);
    type_of_first_element->type_of.expr = first_element;

    ASTNode *value_type = type_of_first_element;

    if (node->iterative.value_by_pointer) {
        ASTNode *pointer = create_node_from_token(ASTNODE_UNARY, current_token);
        pointer->unary.operator = OP_POINTER_TO;
        pointer->unary.operand = type_of_first_element;
        value_type = pointer;
    }

    value->decl.var.type = value_type;
    value->decl.var.init = first_element;
    node->iterative.value = value;

    if (current_token->type == TOK_L_CUPAREN) {
        node->iterative.block = parse_block(parser);
    } else if (current_token->type == TOK_COLON) {
        parser_advance(parser); // ':'
        ASTNode *block = create_node_without_name(ASTNODE_BLOCK, current_token->loc);
        block->statements = parse_statement(parser);
        node->iterative.block = block;
    }

    return node;
}

ASTNode *parse_return(Parser *parser)
{
    ASTNode *node = create_node_from_token(ASTNODE_RETURN, current_token);

    parser_advance(parser); // "return"

    if (current_token->type != TOK_SEMICOLON) {
        node->ret.expr = parse_expression(parser);
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

    if (current_token->type != TOK_R_PAREN) {
        node->call.args = parse_expression_list(parser);
    }
    parser_expect(parser, TOK_R_PAREN);

    return node;
}

ASTNode *parse_dot_operator(Parser *parser, ASTNode *left)
{
    Token *dot_token = parser_expect(parser, TOK_DOT);

    if (current_token->type == TOK_IDENT) {
        Token *member_token = parser_expect(parser, TOK_IDENT);
        ASTNode *node = create_node(ASTNODE_MEMBER_ACCESS, member_token->lexeme, dot_token->loc);
        node->accessed_struct = left;
        return node;
    } else {
        ASTNode *node = create_node_without_name(ASTNODE_UNARY, dot_token->loc);
        node->unary.operator = OP_DEREFERENCE;
        node->unary.operand = left;
        return node;
    }
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

ASTNode *parse_double_plus(Parser *parser, ASTNode *left)
{
    ASTNode *node = create_node_from_token(ASTNODE_BINOP, current_token);
    parser_advance(parser); // "++"

    node->binary.op = operator_from_token_type(TOK_PLUS);
    node->binary.left = left;

    ASTNode *one = create_node_without_name(ASTNODE_NUMBER, current_token->loc);
    one->value._int = 1;
    node->binary.right = one;

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

ASTNode *parse_sqparen_prefix(Parser *parser)
{
    parser_expect(parser, TOK_L_SQPAREN);

    ASTNode *node = create_node_without_name(ASTNODE_ARRAY_LITERAL, current_token->loc);

    size_t saved_pos = parser->pos;
    ASTNode *interval_begin = parse_expression(parser);
    if (current_token->type == TOK_DOUBLE_DOT) {
        node->type = ASTNODE_ARRAY_INTERVAL;
        node->interval.begin = interval_begin;
        if (!node->interval.begin) {
            loc_print(current_token->loc);
            error("Expecting interval begin expression, but got '");
            token_print(current_token);
            printf("'\n");
            exit(1);
        }
        parser_advance(parser); // ".."
        node->interval.end = parse_expression(parser);
        if (!node->interval.end) {
            loc_print(current_token->loc);
            error("Expecting interval end expression, but got '");
            token_print(current_token);
            printf("'\n");
            exit(1);
        }
        if (current_token->type == TOK_COMMA) {
            parser_advance(parser); // ","
            node->interval.step = parse_expression(parser);
            if (!node->interval.step) {
                loc_print(current_token->loc);
                error("Expecting interval step expression, but got '");
                token_print(current_token);
                printf("'\n");
                exit(1);
            }
        }

        if (current_token->type == TOK_R_PAREN) {
            node->interval.end_exclusive = true;
            parser_advance(parser);
        } else if (current_token->type == TOK_R_SQPAREN) {
            node->interval.end_exclusive = false;
            parser_advance(parser);
        } else {
            loc_print(current_token->loc);
            error("Expecting ')' or ']' after interval, but got '");
            token_print(current_token);
            printf("'\n");
            exit(1);
        }
    } else {
        parser->pos = saved_pos;
        node->array.head = parse_expression_list(parser);
        parser_expect(parser, TOK_R_SQPAREN);
    }

    return node;
}

ASTNode *parse_hat_operator(Parser *parser)
{
    ASTNode *node = create_node_from_token(ASTNODE_UNARY, current_token);
    node->unary.operator = OP_POINTER_TO;
    parser_advance(parser); // "^"
    node->unary.operand = parse_expression(parser);
    if (!node->unary.operand) {
        todoln("Report this error");
        exit(1);
    }
    return node;
}

ASTNode *parse_cast(Parser *parser)
{
    parser_expect(parser, TOK_CAST);
    parser_expect(parser, TOK_L_PAREN);
    ASTNode *node = create_node_from_token(ASTNODE_CAST, current_token);
    node->cast.to = parse_expression(parser);
    if (!node->cast.to) {
        loc_print(node->loc);
        errorln("Expecting type expression in cast");
        loc_print(current_token->loc);
        noteln("Something wrong here");
        exit(1);
    }
    parser_expect(parser, TOK_R_PAREN);
    node->cast.expr = parse_expression(parser);
    if (!node->cast.expr) {
        loc_print(node->loc);
        errorln("Expecting expression after cast");
        loc_print(current_token->loc);
        noteln("Something wrong here");
        exit(1);
    }
    return node;
}

ASTNode *parse_array_access(Parser *parser, ASTNode *array)
{
    ASTNode *node = create_node_from_token(ASTNODE_ARRAY_ACCESS, current_token);
    parser_expect(parser, TOK_L_SQPAREN);
    node->array_access.array = array;
    node->array_access.index = parse_expression(parser);
    if (!node->array_access.index) {
        error("Expecting index expression in array access, but got '");
        token_print(current_token);
        printf("'\n");
        exit(1);
    }
    parser_expect(parser, TOK_R_SQPAREN);
    return node;
}

static_assert(__tok_types_count == 41, "Cover all token types in expression_rules table");
ExprRule expression_rules[__tok_types_count] = {
    // No expression rules related to these tokens
    [TOK_IF]         = {NULL, NULL, PREC_NONE},
    [TOK_ELIF]       = {NULL, NULL, PREC_NONE},
    [TOK_ELSE]       = {NULL, NULL, PREC_NONE},
    [TOK_WHILE]      = {NULL, NULL, PREC_NONE},
    [TOK_FOR]        = {NULL, NULL, PREC_NONE},
    [TOK_IN]         = {NULL, NULL, PREC_NONE},
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
    [TOK_EXTERN]     = {NULL, NULL, PREC_NONE},
    [TOK_DOUBLE_DOT] = {NULL, NULL, PREC_NONE},
    [TOK_EOF]        = {NULL, NULL, PREC_NONE},

    // Primaries
    [TOK_ANY]       = {parse_ident, NULL, PREC_NONE},
    [TOK_TYPE]      = {parse_ident, NULL, PREC_NONE}, // TODO: maybe they shouldn't be identifiers
    [TOK_IDENT]     = {parse_ident, NULL, PREC_NONE},
    [TOK_NUMBER]    = {parse_number,         NULL, PREC_NONE},
    [TOK_STRING]    = {parse_string,         NULL, PREC_NONE},
    [TOK_TRUE]      = {parse_true,           NULL, PREC_NONE},
    [TOK_FALSE]     = {parse_false,          NULL, PREC_NONE},
    [TOK_L_SQPAREN] = {parse_sqparen_prefix, parse_array_access, PREC_CALL}, // TODO: what prec?
    [TOK_CAST]      = {parse_cast,           NULL, PREC_NONE}, // TODO: what prec?

    // Arithmetic operators
    [TOK_PLUS]        = {NULL, parse_binary,      PREC_TERM},
    [TOK_STAR]        = {NULL, parse_binary,      PREC_FACTOR},
    [TOK_DOUBLE_PLUS] = {NULL, parse_double_plus, PREC_UNARY},

    // Logical operators
    [TOK_DOUBLE_EQUALS]  = {NULL, parse_binary, PREC_TERM},
    [TOK_NOT_EQUALS]     = {NULL, parse_binary, PREC_TERM},
    [TOK_LESS]           = {NULL, parse_binary, PREC_TERM},
    [TOK_GREATER]        = {NULL, parse_binary, PREC_TERM},
    [TOK_LESS_EQUALS]    = {NULL, parse_binary, PREC_TERM},
    [TOK_GREATER_EQUALS] = {NULL, parse_binary, PREC_TERM},

    // Grouping and others
    [TOK_L_PAREN] = {parse_grouping,     parse_call_infix,   PREC_CALL},
    [TOK_DOT]     = {NULL,               parse_dot_operator, PREC_CALL},
    [TOK_HAT]     = {parse_hat_operator, NULL,               PREC_UNARY},
};

static_assert(__tok_types_count == 41, "Cover all token types in parse_statement");
static_assert(__astnode_types_count == 24, "Cover all ast node types in parsing");
ASTNode *parse_statement(Parser *parser)
{
    Token *token = current_token;

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

    while (current_token->type != TOK_EOF) {
        node->next = parse_statement(parser);
        if (node->next) node = node->next;
    }
    
    return ast;
}

static void register_symbol(Symbol *sym)
{
    if (!scopes.current) {
        errorln("No current scope when registering symbol '%s'", sym->name);
        exit(1);
    }
    sym->scope = scopes.current;
    da_push(&scopes.current->symbols, sym);
}

static void bind_expression(ASTNode *expr);
static TypeInfo *node_type(ASTNode *node);

static_assert(__astnode_types_count == 24, "Cover all ast node types in resolve_type_expression");
TypeInfo *resolve_type_expression(ASTNode *expr)
{
    if (!expr) return primitive_type(PRIMITIVE_VOID);

    switch (expr->type) {
    case ASTNODE_IDENT: {
        PrimitiveType prim = primitive_type_from_string(expr->name);
        if (prim != PRIMITIVE_UNKNOWN) return primitive_type(prim);

        if (streq(expr->name, "Type")) return type_type();
        if (streq(expr->name, "Any")) return any_type();

        Symbol *sym = expr->ident;
        if (!sym) {
            sym = get_symbol(expr->name);
        }
        if (!sym) {
            loc_print(expr->loc);
            errorln("Unknown type '%s'", expr->name);
            exit(1);
        }

        return sym->type;
    }

    case ASTNODE_UNARY: {
        if (expr->unary.operator != OP_POINTER_TO) {
            loc_print(expr->loc);
            error("Invalid unary operator '%s' in type expression\n", operator_as_string(expr->unary.operator));
            exit(1);
        }

        TypeInfo *base = resolve_type_expression(expr->unary.operand);
        TypeInfo *ptr = create_type_without_name(KIND_POINTER);
        ptr->ptr.to = base;
        return ptr;
    }

    case ASTNODE_ARRAY_LITERAL: {
        ASTNode *head = expr->array.head;
        size_t len = node_list_length(expr->array.head);
        if (len != 1 && len != 2) {
            loc_print(expr->loc);
            errorln("Array type '[T, N]' must contain only one type expression and one optional arithmetic expression");
            exit(1);
        }

        TypeInfo *elt = resolve_type_expression(head);
        size_t size = 0;
        if (len == 2) {
            ASTNode *size_expr = head->next;
            if (size_expr->type == ASTNODE_NUMBER) {
                size = size_expr->value._int;
            } else {
                loc_print(size_expr->loc);
                errorln("Array size must be a constant integer");
                exit(1);
            }
        }
        TypeInfo *arr = create_type_without_name(KIND_ARRAY);
        arr->array.of = elt;
        arr->array.size = size;
        return arr;
    }

    case ASTNODE_TYPEOF: {
        bind_expression(expr->type_of.expr);
        return node_type(expr->type_of.expr);
    }

    default:
        loc_print(expr->loc);
        errorln("Expecting a type expression, but got something different");
        exit(1);
    }
}

Symbol *_create_symbol(SymbolKind kind, const char *name, Location loc, bool register_sym)
{
    Symbol *other = get_local_symbol(name);
    if (other) {
        loc_print(loc);
        errorln("Redeclaration of symbol '%s'", name);
        loc_print(other->loc);
        noteln("First declared here");
        exit(1);
    }

    Symbol *sym = calloc(1, sizeof(Symbol));
    assert(sym);

    sym->kind = kind;
    sym->loc = loc;
    sym->name = name;

    if (register_sym) register_symbol(sym);

    return sym;
}
static inline Symbol *create_symbol(SymbolKind kind, const char *name, Location loc)
{
    return _create_symbol(kind, name, loc, false);
}
static inline Symbol *create_declaration_symbol(ASTNode *node)
{
    return _create_symbol(node->decl.kind, node->name, node->loc, false);
}
static inline Symbol *create_symbol_and_register_it(SymbolKind kind, const char *name, Location loc)
{
    return _create_symbol(kind, name, loc, true);
}
static inline Symbol *create_declaration_symbol_and_register_it(ASTNode *node)
{
    return _create_symbol(node->decl.kind, node->name, node->loc, true);
}

void create_types_and_functions_definitions_in_scope(ASTNode *root)
{
    ASTNode *current = root;
    while (current) {
        if (current->type != ASTNODE_DECLARATION || current->decl.kind == SYM_VARIABLE) {
            current = current->next;
            continue;
        }

        if (current->decl.sym) {
            debugln("Can this happen? %u", __LINE__);
        }

        current->decl.sym = create_declaration_symbol_and_register_it(current);
        Symbol *sym = current->decl.sym;

        if (sym->kind == SYM_FUNCTION) {
            if (current->decl.fn.is_external)
                sym->fn.is_external = true;

            ASTNode *fn_decl = current;

            TypeInfo *fn_type = create_type(KIND_FUNCTION, sym->name);
            sym->type = fn_type;

            ASTNode *param_node = fn_decl->decl.fn.params;
            while (param_node) {
                param_node->decl.sym = create_declaration_symbol(param_node);
                Symbol *param_sym = param_node->decl.sym;

                if (param_node->decl.var.type) {
                    TypeInfo *param_type = resolve_type_expression(param_node->decl.var.type);
                    param_sym->type = param_type;
                    da_push(&fn_type->fn.params, param_type);
                } else {
                    // TODO: for now, then infer
                    loc_print(param_node->loc);
                    errorln("Parameter '%s' must have an explicit type", param_sym->name);
                    exit(1);
                }

                param_node = param_node->next;
            }

            ASTNode *fn_ret = fn_decl->decl.fn.ret;
            fn_type->fn.ret_type = fn_ret ? resolve_type_expression(fn_ret) : primitive_type(PRIMITIVE_VOID);
        } else if (sym->kind == SYM_STRUCT) {
            ASTNode *struct_decl = current;
            TypeInfo *struct_type = create_type(KIND_STRUCT, sym->name);
            sym->type = struct_type;

            if (struct_decl->decl.strct.is_generic) {
                struct_type->strct.is_generic = true;

                ASTNode *param_node = struct_decl->decl.strct.params;
                while (param_node) {
                    param_node->decl.sym = create_declaration_symbol(param_node);
                    Symbol *param_sym = param_node->decl.sym;

                    if (param_node->decl.var.type) {
                        TypeInfo *param_type = resolve_type_expression(param_node->decl.var.type);
                        param_sym->type = param_type;
                        da_push(&struct_type->strct.params, param_type);
                    } else {
                        // TODO: for now, then infer
                        loc_print(param_node->loc);
                        errorln("Parameter '%s' must have an explicit type", param_sym->name);
                        exit(1);
                    }

                    param_node = param_node->next;
                }
            }

            ASTNode *member_node = struct_decl->decl.strct.members;
            while (member_node) {
                member_node->decl.sym = create_declaration_symbol(member_node);
                Symbol *member_sym = member_node->decl.sym;

                if (member_node->decl.kind == SYM_VARIABLE) { 
                    if (member_node->decl.var.type) {
                        da_push(&struct_type->strct.members, member_sym);
                    } else {
                        // TODO: for now, then infer
                        loc_print(member_node->loc);
                        errorln("member '%s' must have an explicit type", member_sym->name);
                        exit(1);
                    }
                } else if (member_node->decl.kind == SYM_STRUCT) {
                    da_push(&struct_type->strct.members, member_sym);
                    TypeInfo *nested_type = create_type(KIND_STRUCT, member_sym->name);
                    member_sym->type = nested_type;
                    ASTNode *nested_member = member_node->decl.strct.members;
                    while (nested_member) {
                        nested_member->decl.sym = create_declaration_symbol(nested_member);
                        Symbol *nested_sym = nested_member->decl.sym;
                        if (nested_member->decl.kind == SYM_VARIABLE) {
                            if (nested_member->decl.var.type) {
                                da_push(&nested_type->strct.members, nested_sym);
                            } else {
                                loc_print(nested_member->loc);
                                errorln("member '%s' must have an explicit type", nested_sym->name);
                                exit(1);
                            }
                        }
                        nested_member = nested_member->next;
                    }
                }

                member_node = member_node->next;
            }
        } else {
            unreachableln("DeclarationKind %u in create_types_and_functions_definitions_in_scope", sym->kind);
            exit(1);
        }

        current = current->next;
    }
}

static_assert(__astnode_types_count == 24, "Cover all ast node types in bind_expression");
static void bind_expression(ASTNode *expr)
{
    if (!expr) return;

    switch (expr->type) {
    case ASTNODE_IDENT: {
        if (primitive_type_from_string(expr->name) != PRIMITIVE_UNKNOWN) break;
        if (streq(expr->name, "Type")) break;
        if (streq(expr->name, "Any")) break;
        Symbol *sym = get_symbol(expr->name);
        if (!sym) {
            loc_print(expr->loc);
            errorln("Identifier '%s' is not declared", expr->name);
            exit(1);
        }
        expr->ident = sym;
    } break;

    case ASTNODE_CALL:
    case ASTNODE_CALL_STATEMENT: {
        bind_expression(expr->call.callee);
        ASTNode *arg = expr->call.args;
        while (arg) {
            bind_expression(arg);
            arg = arg->next;
        }
    } break;

    case ASTNODE_FOR:
        break;

    case ASTNODE_MEMBER_ACCESS:
        bind_expression(expr->accessed_struct);
        break;

    case ASTNODE_CAST:
        bind_expression(expr->cast.to);
        bind_expression(expr->cast.expr);
        break;

    case ASTNODE_UNARY:
        bind_expression(expr->unary.operand);
        break;

    case ASTNODE_BINOP:
        bind_expression(expr->binary.left);
        bind_expression(expr->binary.right);
        break;

    case ASTNODE_ARRAY_LITERAL: {
        ASTNode *elt = expr->array.head;
        while (elt) {
            bind_expression(elt);
            elt = elt->next;
        }
    } break;

    case ASTNODE_ARRAY_INTERVAL: {
        bind_expression(expr->interval.begin);
        bind_expression(expr->interval.end);
        bind_expression(expr->interval.step);
    } break;

    case ASTNODE_ARRAY_ACCESS: {
        bind_expression(expr->array_access.array);
        bind_expression(expr->array_access.index);
    } break;

    case ASTNODE_TYPEOF: {
        bind_expression(expr->type_of.expr);
    } break;

    default: break;
    }
}

static void bind_declaration(ASTNode *node);
static void bind_declaration_list(ASTNode *list);

static void bind_struct_declaration(ASTNode *node)
{
    const char *prefix = scopes.current ? scopes.current->prefix : NULL;
    uint64_t scope_id = scopes.current && !scopes.current->is_global ? scopes.current->id : 0;

    if (!node->decl.sym) {
        node->decl.sym = create_declaration_symbol_and_register_it(node);
    } else {
        Symbol *existing = get_local_symbol(node->name);
        if (!existing) {
            register_symbol(node->decl.sym);
        } else {
            // TODO ?
        }
    }

    push_new_scope_with_prefix(node->name);

    TypeInfo *struct_type = node->decl.sym->type;
    if (!struct_type || struct_type->kind != KIND_STRUCT) {
        struct_type = create_type(KIND_STRUCT, node->name);
        node->decl.sym->type = struct_type;
    }
    struct_type->strct.prefix = prefix;
    struct_type->strct.scope_id = scope_id;

    ASTNode *p = node->decl.strct.params;
    while (p) {
        bind_declaration(p);
        da_push(&node->decl.sym->strct.params, p->decl.sym);
        if (p->decl.sym->type) {
            da_push(&struct_type->strct.params, p->decl.sym->type);
        }
        p = p->next;
    }

    ASTNode *m = node->decl.strct.members;
    while (m) {
        if (m->decl.kind != SYM_VARIABLE) {
            bind_declaration(m);
        }
        m = m->next;
    }

    m = node->decl.strct.members;
    while (m) {
        if (m->decl.kind == SYM_VARIABLE) {
            if (!m->decl.sym) {
                m->decl.sym = create_declaration_symbol(m);
            }
            m->decl.sym->scope = scopes.current;
            if (m->decl.var.type) {
                bind_expression(m->decl.var.type);
                m->decl.sym->type = resolve_type_expression(m->decl.var.type);
            }
            m->type_checked = true;
        }
        m = m->next;
    }

    bind_declaration_list(node->decl.strct.params);
    bind_declaration_list(node->decl.strct.members);

    pop_scope();
}

static void bind_function_declaration(ASTNode *node) {
    push_new_scope_with_prefix(node->name);

    ASTNode *p = node->decl.fn.params;
    while (p) {
        register_symbol(p->decl.sym);
        da_push(&node->decl.sym->fn.params, p->decl.sym);
        p = p->next;
    }

    bind_declaration_list(node->decl.fn.block);

    pop_scope();
}

static void bind_variable_declaration(ASTNode *node)
{
    if (node->decl.sym) return;

    node->decl.sym = create_declaration_symbol_and_register_it(node);

    if (node->decl.var.type) {
        bind_expression(node->decl.var.type);
    }
    if (node->decl.var.init) {
        bind_expression(node->decl.var.init);
    }
}

static_assert(__symbol_kinds_count == 3, "Bind each declaration kind");
static void bind_declaration(ASTNode *node)
{
    assert(node->type == ASTNODE_DECLARATION);

    switch (node->decl.kind) {
    case SYM_VARIABLE: bind_variable_declaration(node); break;
    case SYM_FUNCTION: bind_function_declaration(node); break;
    case SYM_STRUCT:   bind_struct_declaration(node); break;
    default:
        unreachableln("DeclarationKind %u in bind_declaration", node->decl.kind);
        exit(1);
    }
}

static_assert(__astnode_types_count == 24, "Cover all ast node types in bind_declaration_list");
static void bind_declaration_list(ASTNode *list)
{
    ASTNode *current = list;
    while (current) {
        switch (current->type) {
        case ASTNODE_DECLARATION: {
            bind_declaration(current);

            if (current->decl.kind == SYM_VARIABLE && current->decl.var.init) {
                bind_expression(current->decl.var.init->assign.lhs);
                bind_expression(current->decl.var.init->assign.rhs);
            }
        } break;

        case ASTNODE_ASSIGNMENT:
            bind_expression(current->assign.lhs);
            bind_expression(current->assign.rhs);
            break;

        case ASTNODE_CALL:
        case ASTNODE_CALL_STATEMENT:
            bind_expression(current);
            break;

        case ASTNODE_RETURN:
            if (current->ret.expr) bind_expression(current->ret.expr);
            break;

        case ASTNODE_BLOCK:
            push_new_scope(); {
                create_types_and_functions_definitions_in_scope(current->statements);
                bind_declaration_list(current->statements);
            } pop_scope();
            break;

        case ASTNODE_IF:
        case ASTNODE_WHILE:
            bind_expression(current->conditional.expr);
            bind_declaration_list(current->conditional.block);
            bind_declaration_list(current->conditional.list);
            break;

        case ASTNODE_ELIF:
            bind_expression(current->conditional.expr);
            bind_declaration_list(current->conditional.block);
            bind_declaration_list(current->next);
            break;

        case ASTNODE_ELSE:
            bind_declaration_list(current->conditional.block);
            break;

case ASTNODE_FOR:
            push_new_scope();
            {
                bind_declaration(current->iterative.value);
                bind_expression(current->iterative.list);
                push_new_scope();
                if (current->iterative.index) bind_declaration(current->iterative.index);
                bind_declaration_list(current->iterative.block);
                pop_scope();
            }
            pop_scope();
            break;

        default: break;
        }

        current = current->next;
    }
}

void bind_symbols(ASTNode *root) {
    if (!root) return;
    if (root->type != ASTNODE_PROGRAM) {
        unreachableln("bind_symbols must be called on ast root");
        exit(1);
    }

    push_new_scope(); { // global scope
        create_types_and_functions_definitions_in_scope(root->next);
        bind_declaration_list(root->next);
    } pop_scope();
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
        DEFAULT_KJUDE_INTERMEDIATE_C_FILE,
        "-o", "output",
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

TypeInfo *unary_type(ASTNode *node);
TypeInfo *binop_type(ASTNode *node);

static_assert(__astnode_types_count == 24, "Cover all ast node types in node_type");
TypeInfo *node_type(ASTNode *node)
{
    if (!node) {
        unreachableln("NULL node in node_type");
        exit(1);
    }

    if (node->resolved_type) return node->resolved_type;

    TypeInfo *result_type = NULL;

    switch (node->type) {
    case ASTNODE_MEMBER_ACCESS: {
        TypeInfo *struct_type = node_type(node->accessed_struct);
        if (struct_type->kind == KIND_POINTER) {
            struct_type = struct_type->ptr.to;
        }
        result_type = get_struct_member(struct_type, node->name)->type;
    } break;

    case ASTNODE_CALL:
    case ASTNODE_CALL_STATEMENT: {
        result_type = node_type(node->call.callee)->fn.ret_type;
    } break;

    case ASTNODE_CAST: result_type = node_type(node->cast.to); break;

    case ASTNODE_UNARY: result_type = unary_type(node); break;
    case ASTNODE_BINOP: result_type = binop_type(node); break;

    case ASTNODE_NUMBER: result_type = primitive_type(PRIMITIVE_INT);    break;
    case ASTNODE_STRING: result_type = primitive_type(PRIMITIVE_STRING); break;
    case ASTNODE_BOOL:   result_type = primitive_type(PRIMITIVE_BOOL);   break;

    case ASTNODE_ARRAY_LITERAL: {
        TypeInfo *arr = create_type_without_name(KIND_ARRAY);
        size_t size = node_list_length(node->array.head);
        arr->array.size = size;
        if (size > 0) {
            arr->array.of = node_type(node->array.head);
        }
        result_type = arr;
    } break;

    case ASTNODE_ARRAY_INTERVAL: {
        TypeInfo *arr = create_type_without_name(KIND_ARRAY);
        arr->array.of = primitive_type(PRIMITIVE_INT);
        //arr->array.size = ?; // TODO: technically it's (end-begin)/step, but I don't have numbers, I have nodes

        result_type = arr;
    } break;

    case ASTNODE_ARRAY_ACCESS: {
        TypeInfo *array_type = node_type(node->array_access.array);
        result_type = array_type->array.of;
    } break;

    case ASTNODE_TYPEOF: result_type = node_type(node->type_of.expr); break;

    case ASTNODE_IDENT: {
        PrimitiveType prim = primitive_type_from_string(node->name);
        if (prim != PRIMITIVE_UNKNOWN) {
            result_type = primitive_type(prim);
            break;
        }
        if (streq(node->name, "Type")) {
            result_type = type_type();
            break;
        }
        if (streq(node->name, "Any")) {
            result_type = any_type();
            break;
        }
        Symbol *sym = node->ident;
        if (!sym) {
            loc_print(node->loc);
            errorln("Identifier '%s' is not bound to a symbol", node->name);
            exit(1);
        }
        if (!sym->type || sym->type->kind == KIND_INVALID) {
            loc_print(sym->loc);
            unreachableln("Identifier '%s' has no valid type", sym->name);
            exit(1);
        }
        result_type = sym->type;
    } break;

    default:
        unreachableln("Ast node type '%s' in node_type", astnode_type_as_string(node->type));
        exit(1);
    }

    if (!result_type || result_type->kind == KIND_INVALID) {
        loc_print(node->loc);
        unreachableln("Node type '%s' (%s)", astnode_type_as_string(node->type), node->name);
        unreachableln("Type could not be determined or it's invalid in type check");
        exit(1);
    }

    node->resolved_type = result_type;
    return result_type;
}

static_assert(__op_types_count == 8, "Cover all unary operators in binop_type");
TypeInfo *unary_type(ASTNode *node)
{
    TypeInfo *operand_type = node_type(node->unary.operand);

    //loc_print(node->loc);
    //debug("Unary operation: %s ", operator_as_string(node->unary.operator));
    //type_print(operand_type);
    //printf("\n");

    switch (node->unary.operator) {
    case OP_POINTER_TO:
        TypeInfo *pointer_type = create_type_without_name(KIND_POINTER);
        pointer_type->ptr.to = operand_type;
        return pointer_type;

    case OP_DEREFERENCE:
        if (operand_type->kind != KIND_POINTER) {
            loc_print(node->loc);
            error("Cannot dereference '%s' of type '", node->unary.operand->name);
            type_print(operand_type);
            printf("'\n");
            exit(1);
        }
        return operand_type->ptr.to;

    default:
        unreachableln("Operator %u in unary_type", node->unary.operator);
        exit(1);
    }

    loc_print(node->loc);
    error("Invalid unary operation: %s'", operator_as_string(node->unary.operator));
    type_print(operand_type);
    printf("'\n");
    exit(1);
}

// TODO: here I can do shortcircuit optimizations
static_assert(__op_types_count == 8, "Cover all binary operators in binop_type");
TypeInfo *binop_type(ASTNode *node)
{
    TypeInfo *left  = node_type(node->binary.left);
    TypeInfo *right = node_type(node->binary.right);

    //loc_print(node->loc);
    //debugln("Binary operation: ('");
    //type_print(left);
    //printf("' %s '", operator_as_string(node->binary.op));
    //type_print(right);
    //printf("')\n");

    switch (node->binary.op) {
    case OP_PLUS:
        if (are_types_equal(left, right)) return left;
        break;
    case OP_STAR:
        if (are_types_equal(left, right)) return left;
        break;

    case OP_DOUBLE_EQUALS:
    case OP_NOT_EQUALS:
    case OP_LESS_EQUALS:
    case OP_GREATER_EQUALS:
        if (are_types_equal(left, right)) return primitive_type(PRIMITIVE_BOOL);
        break;

    default:
        unreachableln("Operator %u in binop_type", node->binary.op);
        exit(1);
    }

    loc_print(node->loc);
    error("Invalid binary operation: ('");
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
            error("Mismatched argument %zu: wanted '", i+1);
            type_print(formal);
            printf("' but got '");
            type_print(actual);
            printf("'\n");
            exit(1);
        }
        current = current->next; 
        i++;
    }
}

bool are_types_compatible(TypeInfo *left, TypeInfo *right)
{
    debug("Are '");
    type_print(left);
    printf("' and '");
    type_print(right);
    printf("' compatible?\n");

    if (are_types_equal(left, right)) return true;
    if (right->kind == KIND_ANY) return true;
    if (left->kind == KIND_POINTER && right->kind == KIND_POINTER) {
        return are_types_compatible(left->ptr.to, right->ptr.to);
    }

    return false;
}

static_assert(__astnode_types_count == 24, "Cover all ast node types in type_check");
void type_check(ASTNode *node)
{
    if (!node) return;
    if (node->type_checked) return;

    switch (node->type) {
    case ASTNODE_PROGRAM: {
        type_check(node->next);
    } break;

    case ASTNODE_DECLARATION: {
        Symbol *sym = node->decl.sym;
        switch (sym->kind) {
        case SYM_VARIABLE: {
            if (node->decl.var.type) {
                sym->type = resolve_type_expression(node->decl.var.type);
            } else {
                sym->type = node_type(node->decl.var.init->assign.rhs);
                //debug("Inferred type of '%s' -> '", node->decl.sym->name);
                //type_print(sym->type);
                //printf("'\n");
            }

            if (node->decl.var.init) {
                type_check(node->decl.var.init);
            }
        } break;
        case SYM_FUNCTION: {
            push_current_function(sym);

            ASTNode *param_node = node->decl.fn.params;
            while (param_node) {
                type_check(param_node);
                param_node = param_node->next;
            }

            type_check(node->decl.fn.block);

            pop_current_function();
        } break;
        case SYM_STRUCT: {
            if (node->decl.strct.is_generic) {
                ASTNode *param_node = node->decl.strct.params;
                while (param_node) {
                    type_check(param_node);
                    param_node = param_node->next;
                }
                for (size_t i = 0; i < node->decl.sym->strct.params.count; i++) {
                    Symbol *param = node->decl.sym->strct.params.items[i];
                    da_push(&sym->type->strct.params, param->type);
                }
            }

            ASTNode *member_node = node->decl.strct.members;
            while (member_node) {
                type_check(member_node);
                member_node = member_node->next;
            }
        } break;
        default: unreachableln("SymbolKind %u in declaration node in type_check", sym->kind); exit(1);
        }

        type_check(node->next);
    } break;

    case ASTNODE_ASSIGNMENT: {
        type_check(node->assign.lhs);
        type_check(node->assign.rhs);
        TypeInfo *lhs_type = node_type(node->assign.lhs);
        match_type(node->assign.rhs, lhs_type);
        type_check(node->next);
    } break;

    case ASTNODE_BLOCK: {
        type_check(node->statements);
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

            node->name = fn->name;

            node->call.callee->type = ASTNODE_IDENT;
            node->call.callee->name = fn->name;
            node->call.callee->ident = fn;

            if (!fn->type->fn.is_static) {
                ASTNode *this_arg = calloc(1, sizeof(ASTNode));
                *this_arg = *instance;
                this_arg->next = node->call.args;
                node->call.args = this_arg;
            }
        } else fn = node->call.callee->ident;

        match_arguments(node->call.args, fn->type, fn->loc, node->loc);

        if (node->type == ASTNODE_CALL_STATEMENT) {
            type_check(node->next);
        }
    } break;

    case ASTNODE_CAST: {
        type_check(node->cast.to);
        type_check(node->cast.expr);
        TypeInfo *expr_type = node_type(node->cast.expr);
        TypeInfo *cast_type = resolve_type_expression(node->cast.to);
        if (!are_types_compatible(cast_type, expr_type)) {
            error("Cannot cast '");
            type_print(expr_type);
            printf("' to '");
            type_print(cast_type);
            printf("'\n");
            exit(1);
        }
    } break;
    
    case ASTNODE_MEMBER_ACCESS: {
        type_check(node->accessed_struct);
        TypeInfo *struct_type = node_type(node->accessed_struct);

        if (struct_type->kind == KIND_POINTER) {
            struct_type = struct_type->ptr.to;
        }

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
        type_check(node->conditional.expr);
        match_type(node->conditional.expr, primitive_type(PRIMITIVE_BOOL));
        type_check(node->conditional.block);
        type_check(node->next);
    } break;

    case ASTNODE_FOR: {
        type_check(node->iterative.list);
        TypeInfo * list_type = node_type(node->iterative.list);
        if (list_type->kind != KIND_ARRAY) {
            loc_print(node->iterative.list->loc);
            error("Type '");
            type_print(list_type);
            printf("' is not iterable\n");
            exit(1);
        }

        if (node->iterative.list->type == ASTNODE_ARRAY_INTERVAL && node->iterative.value_by_pointer) {
            loc_print(node->iterative.value->loc);
            errorln("Cannot take pointer to range iterator");
            exit(1);
        }

        if (node->iterative.value_by_pointer && list_type->array.size == 0) {
            loc_print(node->iterative.value->loc);
            errorln("Cannot take pointer to dynamic array iterator");
            exit(1);
        }

        type_check(node->iterative.value);
        if (node->iterative.index) type_check(node->iterative.index);

        type_check(node->iterative.block);
        type_check(node->next);
    } break;

    case ASTNODE_RETURN: {
        Symbol *fn_sym = get_current_function();
        if (!fn_sym) {
            loc_print(node->loc);
            errorln("Invalid return outside of function");
            exit(1);
        }

        TypeInfo *expected_ret = fn_sym->type->fn.ret_type;

        if (!node->ret.expr && !are_types_equal(expected_ret, primitive_type(PRIMITIVE_VOID))) {
            loc_print(node->loc);
            error("Function '%s' expectes '", fn_sym->name);
            type_print(expected_ret);
            printf("' as return type\n");
            exit(1);
        } else {
            match_type(node->ret.expr, expected_ret);
        }

        if (node->next) {
            loc_print(node->next->loc);
            warningln("Unreachable code after return statement");
            type_check(node->next); // TODO: do I type check it anyway?
        }
    } break;

    case ASTNODE_UNARY: {
        type_check(node->unary.operand);
        if (node->unary.operator == OP_POINTER_TO) {
            ASTNode *pointed = node->unary.operand;
            if (pointed->type != ASTNODE_IDENT) {
                loc_print(pointed->loc);
                errorln("Cannot take address of non lvalue expression");
                exit(1);
            }
        }
    } break;

    case ASTNODE_BINOP: {
        type_check(node->binary.left);
        type_check(node->binary.right);
    } break;

    case ASTNODE_IDENT:
    case ASTNODE_NUMBER:
    case ASTNODE_STRING:
    case ASTNODE_BOOL:
        break;

    case ASTNODE_ARRAY_LITERAL: {
        TypeInfo *array_type = node_type(node);
        if (array_type->array.size > 0) {
            TypeInfo *candidate_type = array_type->array.of;
            ASTNode *elt = node->array.head->next;
            while (elt) {
                TypeInfo *elt_type = node_type(elt);
                if (!are_types_equal_with_errors(candidate_type, elt_type, NULL, NULL)) {
                    loc_print(elt->loc);
                    error("Different types in array literal: expecting '");
                    type_print(candidate_type);
                    printf("' but got '");
                    type_print(elt_type);
                    printf("'\n");
                    exit(1);
                }
                elt = elt->next;
            }
        }
    } break;

    case ASTNODE_ARRAY_INTERVAL: {
        type_check(node->interval.begin);
        match_type(node->interval.begin, primitive_type(PRIMITIVE_INT));

        type_check(node->interval.end);
        match_type(node->interval.end, primitive_type(PRIMITIVE_INT));

        if (node->interval.step) {
            type_check(node->interval.step);
            match_type(node->interval.step, primitive_type(PRIMITIVE_INT));
        }
    } break;

    case ASTNODE_ARRAY_ACCESS: {
        type_check(node->array_access.array);
        TypeInfo *array_type = node_type(node->array_access.array);
        if (array_type->kind != KIND_ARRAY) {
            error("Cannot access index of type '");
            type_print(array_type);
            printf("'\n");
            exit(1);
        }
        type_check(node->array_access.index);
    } break;

    case ASTNODE_TYPEOF: type_check(node->type_of.expr); break;

    default:
        unreachableln("Ast node type '%s' in type_check", astnode_type_as_string(node->type));
        exit(1);
    }

    node->type_checked = true;
}

void generate_fn_signature(Symbol *sym, FILE *f)
{
    if (sym->type->fn.ret_type->kind == KIND_ARRAY) {
        todoln("Return list from function");
        exit(1);
    }

    generate_type(sym->type->fn.ret_type, NULL, f, sym);
    fprintf(f, " ");
    if (sym->scope->prefix) fprintf(f, "%s_", sym->scope->prefix);
    fprintf(f, "%s", sym->name);
    if (!sym->scope->is_global) fprintf(f, "_%lu", sym->scope->id);
    fprintf(f, "(");
    if (da_is_empty(&sym->fn.params)) {
        fprintf(f, "void");
    } else {
        for (size_t i = 0; i < sym->fn.params.count; i++) {
            Symbol *param = sym->fn.params.items[i];
            if (param->type->kind == KIND_FUNCTION) {
                generate_fn_signature(param, f);
            } else {
                generate_type(param->type, param->name, f, param);
            }
            if (i < sym->fn.params.count-1) fprintf(f, ", ");
        }
    }
    fprintf(f, ")");
}

static void generate_symbol_name(Symbol *sym, FILE *f)
{
    if (sym->scope && sym->scope->prefix) fprintf(f, "%s_", sym->scope->prefix);
    fprintf(f, "%s", sym->name);
    if (sym->scope && !sym->scope->is_global) fprintf(f, "_%lu", sym->scope->id);
}

static inline void generate_struct_type(Symbol *sym, FILE *f)
{
    fprintf(f, "typedef struct ");
    generate_symbol_name(sym, f);
    fprintf(f, " ");
    generate_symbol_name(sym, f);
    fprintf(f, ";\n");
}

void _generate_struct(Symbol *strct, FILE *f, bool with_type)
{
    if (with_type) fprintf(f, "typedef struct ");
    else {
        fprintf(f, "struct ");
        generate_symbol_name(strct, f);
        fprintf(f, " ");
    }
    fprintf(f, "{\n");
    for (size_t i = 0; i < strct->type->strct.members.count; i++) {
        Symbol *member = strct->type->strct.members.items[i];
        if (member->kind != SYM_VARIABLE) continue;
        generate_type(member->type, member->name, f, member);
        fprintf(f, ";\n");
    }
    fprintf(f, "}");
    if (with_type) {
        fprintf(f, " ");
        generate_symbol_name(strct, f);
    }
    fprintf(f, ";\n");
}
static inline void generate_struct_with_type(Symbol *sym, FILE *f) { _generate_struct(sym, f, true); }
static inline void generate_struct(Symbol *sym, FILE *f) { _generate_struct(sym, f, false); }

void extract_declarations(ASTNode *node, Nodes *type_defs, Nodes *struct_defs, Nodes *fn_decls, Nodes *fn_defs, Nodes *glob_var_decls, Nodes *glob_var_defs)
{
    while (node) {
        switch (node->type) {
        case ASTNODE_DECLARATION:
            Symbol *sym = node->decl.sym;
            switch (sym->kind) {
            case SYM_VARIABLE:
                if (!sym->scope) {
                    unreachableln("Symbol '%s' scope should have been set", sym->name);
                    exit(1);
                }
                if (!sym->scope->is_global) break;
                if (sym->var.initialized) da_push(glob_var_defs, node);
                else da_push(glob_var_decls, node);
                break;
            case SYM_FUNCTION:
                da_push(fn_decls, node);
                if (sym->fn.is_external) break;
                da_push(fn_defs, node);
                extract_declarations(node->decl.fn.block, type_defs, struct_defs, fn_decls, fn_defs, glob_var_decls,
                        glob_var_defs);
                break;
            case SYM_STRUCT:
                extract_declarations(node->decl.strct.members, type_defs, struct_defs, fn_decls, fn_defs, glob_var_decls, glob_var_defs);
                da_push(type_defs, node);
                da_push(struct_defs, node);
                break;
            default: unreachableln("SymbolKind %d in extract_declarations", sym->kind); exit(1);
            }
            break;
        case ASTNODE_BLOCK:
            extract_declarations(node->statements, type_defs, struct_defs, fn_decls, fn_defs, glob_var_decls,
                    glob_var_defs);
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

        case ASTNODE_FOR:
            extract_declarations(node->iterative.block, type_defs, struct_defs, fn_decls, fn_defs, glob_var_decls, glob_var_defs);
            break;

        default:
            break;
        }
        node = node->next;
    }
}

void generate_c_code(ASTNode *node, FILE *f);
void generate_unary_operation(ASTNode *node, FILE *f)
{
    switch (node->unary.operator) {
    case OP_POINTER_TO:    fprintf(f, "&("); generate_c_code(node->unary.operand, f); fprintf(f, ")"); break;
    case OP_DEREFERENCE:   fprintf(f, "*("); generate_c_code(node->unary.operand, f); fprintf(f, ")"); break;
    default:
        unreachableln("Operator %u in generate_unary_operation", node->unary.operator);
        exit(1);
    }
}

void generate_runtime(FILE *f)
{
    String runtime = {0};
    if (!read_entire_file(KJUDE_RUNTIME_PATH, &runtime)) {
        errorln("Could read runtime file at "KJUDE_RUNTIME_PATH": %s", strerror(errno));
        exit(1);
    }
    fprintf(f, "// This file has been generated by Kjude compiler\n\n");

    fprintf(f, "/* === Kjude Runtime === */\n\n");
    fprintf(f, S_FMT"\n", S_ARG(runtime));
    fprintf(f, "/* ===================== */\n\n");
}

static_assert(__astnode_types_count == 24, "Cover all ast node types in generate_c_code");
void generate_c_code(ASTNode *node, FILE *f)
{
    if (!node) return;

    switch (node->type) {
    case ASTNODE_PROGRAM: {
        generate_runtime(f);

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
            generate_type(var->type, var->name, f, var);
            fprintf(f, ";\n");
        }
        if (glob_var_decls.count > 0) fprintf(f, "\n");

        for (size_t i = 0; i < fn_defs.count; i++) {
            Symbol *sym = fn_defs.items[i]->decl.sym;
            generate_fn_signature(sym, f);
            fprintf(f, " ");
            push_current_function(sym);
            generate_c_code(fn_defs.items[i]->decl.fn.block, f);
            pop_current_function();
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
        Symbol *sym = node->decl.sym;
        switch (sym->kind) {
        case SYM_VARIABLE: {
            if (sym->scope->is_global) break; // global variables have been already been generated
            char var_name[256];
            if (sym->scope->prefix) {
                snprintf(var_name, sizeof(var_name), "%s_%s_%lu", sym->scope->prefix, sym->name, sym->scope->id);
            } else {
                snprintf(var_name, sizeof(var_name), "%s_%lu", sym->name, sym->scope->id);
            }
            generate_type(sym->type, var_name, f, sym);
            if (node->decl.var.init) {
                fprintf(f, " = ");
                generate_c_code(node->decl.var.init->assign.rhs, f);
            }
            fprintf(f, ";\n");
        } break;
        case SYM_FUNCTION:
        case SYM_STRUCT: 
            break;
        default: unreachableln("SymbolKind %d generating declaration node", sym->kind); exit(1);
        }

        generate_c_code(node->next, f);
    } break;

    case ASTNODE_ASSIGNMENT: {
        TypeInfo *lhs_type = node->assign.lhs->resolved_type;

        if (lhs_type->kind == KIND_ARRAY && !node->assign.is_init) {
            fprintf(f, "memcpy(");
            generate_c_code(node->assign.lhs, f);
            fprintf(f, ", (");
            generate_type(lhs_type->array.of, NULL, f, NULL);
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
        generate_c_code(node->statements, f);
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
        bool is_range = node->iterative.list->type == ASTNODE_ARRAY_INTERVAL;
        Symbol *val_sym = node->iterative.value->decl.sym;

        char val_name[256];
        if (val_sym->scope->prefix) {
            snprintf(val_name, sizeof(val_name), "%s_%s_%lu", val_sym->scope->prefix, val_sym->name, val_sym->scope->id);
        } else {
            snprintf(val_name, sizeof(val_name), "%s_%lu", val_sym->name, val_sym->scope->id);
        }

        if (is_range) {
            TypeInfo *iter_type = node->iterative.value_by_pointer
                ? val_sym->type->ptr.to
                : val_sym->type;
            uint64_t scope_id = val_sym->scope ? val_sym->scope->id : 0;

            fprintf(f, "int32_t __for_begin_%lu = ", scope_id);
            generate_c_code(node->iterative.list->interval.begin, f);
            fprintf(f, ";\n");

            fprintf(f, "int32_t __for_end_%lu = ", scope_id);
            generate_c_code(node->iterative.list->interval.end, f);
            fprintf(f, ";\n");

            fprintf(f, "int32_t __for_step_%lu = ", scope_id);
            if (node->iterative.list->interval.step) {
                generate_c_code(node->iterative.list->interval.step, f);
            } else {
                fprintf(f, "1");
            }
            fprintf(f, ";\n");

            const char *op = node->iterative.list->interval.end_exclusive ? " < " : " <= ";

            fprintf(f, "for (");
            generate_type(iter_type, val_name, f, val_sym);
            fprintf(f, " = __for_begin_%lu; ", scope_id);
            fprintf(f, "%s %s ", val_name, op);
            fprintf(f, "__for_end_%lu; ", scope_id);
            fprintf(f, "%s += __for_step_%lu)", val_name, scope_id);

            generate_c_code(node->iterative.block, f);
            fprintf(f, "\n");
        } else {
            TypeInfo *array_type = node_type(node->iterative.list);
            if (array_type->array.size == 0) {
                todoln("FOR loop over dynamic arrays not yet supported");
                exit(1);
            }

            fprintf(f, "auto __for_begin = ");
            generate_c_code(node->iterative.list, f);
            fprintf(f, ";\n");

            fprintf(f, "auto __for_end = __for_begin + %zu;\n", array_type->array.size);

            TypeInfo *iter_type = node->iterative.value_by_pointer
                ? val_sym->type->ptr.to
                : val_sym->type;

            fprintf(f, "for (");
            generate_type(iter_type, val_name, f, val_sym);
            fprintf(f, " = __for_begin; ");
            fprintf(f, "%s", val_name);
            fprintf(f, " < __for_end; ");
            fprintf(f, "%s++)", val_name);

            generate_c_code(node->iterative.block, f);
            fprintf(f, "\n");
        }

        generate_c_code(node->next, f);
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

    case ASTNODE_CAST: {
        fprintf(f, "((");
        generate_type(node_type(node->cast.to), NULL, f, NULL);
        fprintf(f, ")");
        generate_c_code(node->cast.expr, f);
        fprintf(f, ")");
    } break;

    case ASTNODE_MEMBER_ACCESS: {
        generate_c_code(node->accessed_struct, f);
        if (node->accessed_struct->resolved_type->kind == KIND_POINTER) fprintf(f, "->");
        else fprintf(f, ".");
        fprintf(f, "%s", node->name);
    } break;

    case ASTNODE_UNARY: generate_unary_operation(node, f); break;

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

    case ASTNODE_ARRAY_LITERAL: {
        fprintf(f, "{");
        ASTNode *elt = node->array.head;
        while (elt) {
            generate_c_code(elt, f);
            if (elt->next) fprintf(f, ", ");
            elt = elt->next;
        }
        fprintf(f, "}");
    } break;

    case ASTNODE_ARRAY_INTERVAL: {
        todoln("Generate array interval (use in FOR loop)");
        exit(1);
    } break;

    case ASTNODE_ARRAY_ACCESS: {
        generate_c_code(node->array_access.array, f);
        fprintf(f, "[");
        generate_c_code(node->array_access.index, f);
        fprintf(f, "]");
    } break;


    case ASTNODE_TYPEOF: {
        todoln("Generate typeof");
        exit(1);
    } break;

    case ASTNODE_IDENT: {
        if (node->ident->scope && node->ident->scope->is_global) {
            fprintf(f, "%s", node->ident->name);
            break;
        }
        Symbol *current_fn = get_current_function();
        bool is_param = false;
        if (current_fn) {
            for (size_t i = 0; i < current_fn->fn.params.count; i++) {
                if (current_fn->fn.params.items[i] == node->ident) {
                    is_param = true;
                    break;
                }
            }
        }
        if (is_param) {
            fprintf(f, "%s", node->ident->name);
        } else {
            if (node->ident->scope && node->ident->scope->prefix) fprintf(f, "%s_", node->ident->scope->prefix);
            fprintf(f, "%s", node->ident->name);
            if (node->ident->scope) fprintf(f, "_%lu", node->ident->scope->id);
        }
    } break;

    default:
        unreachableln("Ast node type '%s' in generate_c_code", astnode_type_as_string(node->type));
        exit(1);
    }
}

static_assert(__primitive_types_count == 5-1, "Instantiate all primitive types in initialize");
void initialize(void)
{
    _type_type = (TypeInfo){ .kind = KIND_TYPE };
    _type_any  = (TypeInfo){ .kind = KIND_ANY };

    _type_primitive_void   = (TypeInfo){ .kind = KIND_PRIMITIVE, .primitive = PRIMITIVE_VOID };
    _type_primitive_int    = (TypeInfo){ .kind = KIND_PRIMITIVE, .primitive = PRIMITIVE_INT };
    //_type_primitive_uint   = (TypeInfo){ .kind = KIND_PRIMITIVE, .primitive = PRIMITIVE_UINT };
    _type_primitive_string = (TypeInfo){ .kind = KIND_PRIMITIVE, .primitive = PRIMITIVE_STRING };
    _type_primitive_bool   = (TypeInfo){ .kind = KIND_PRIMITIVE, .primitive = PRIMITIVE_BOOL };

    TypeInfo *main_fn;

    main_fn = create_type_without_name(KIND_FUNCTION);
    main_fn->fn.ret_type = primitive_type(PRIMITIVE_VOID);
    _types_main_fn[0] = main_fn; // (void) -> void

    main_fn = create_type_without_name(KIND_FUNCTION);
    main_fn->fn.ret_type = primitive_type(PRIMITIVE_INT);
    _types_main_fn[1] = main_fn; // (void) -> int

    TypeInfo *main_args_type = create_type_without_name(KIND_ARRAY);
    main_args_type->array.of = primitive_type(PRIMITIVE_STRING);

    main_fn = create_type_without_name(KIND_FUNCTION);
    main_fn->fn.ret_type = primitive_type(PRIMITIVE_VOID);
    da_push(&main_fn->fn.params, main_args_type);
    _types_main_fn[2] = main_fn; // ([string]) -> void

    main_fn = create_type_without_name(KIND_FUNCTION);
    main_fn->fn.ret_type = primitive_type(PRIMITIVE_INT);
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

    /// Begin Symbol binding
    Timer symbol_binding_timer = {0};
    timer_start(&symbol_binding_timer);

    bind_symbols(ast);

    timer_finish(&symbol_binding_timer, "Symbol binding");
    /// End Symbol binding

    /// Begin Type checking
    Timer type_check_timer = {0};
    timer_start(&type_check_timer);

    type_check(ast);

    // TODO: capire dove metterlo
    //Symbol *main_fn = get_function_symbol("main");
    //if (!main_fn) {
    //    errorln("Entry point function 'main' is not declared");
    //    exit(1);
    //} else {
    //    bool ok = false;
    //    for (int i = 0; i < MAIN_FN_ALTERNATIVES; i++) {
    //        if (are_types_equal(main_fn->type, _types_main_fn[i])) {
    //            ok = true;
    //            break;
    //        }
    //    }
    //    if (!ok) {
    //        loc_print(main_fn->loc);
    //        errorln("Mismatched entry point function 'main' type declaration");
    //        note("Got ");
    //        type_print(main_fn->type);
    //        printf("\n");
    //        noteln("But expected one of these %u alternatives:", MAIN_FN_ALTERNATIVES);
    //        for (int i = 0; i < MAIN_FN_ALTERNATIVES; i++) {
    //            note("%d: ", i+1);
    //            type_print(_types_main_fn[i]);
    //            printf("\n");
    //        }
    //        exit(1);
    //    }
    //}

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
