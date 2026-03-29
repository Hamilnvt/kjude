#include <stdio.h>
#include <stddef.h>

static size_t _dot_id = 0;
static inline size_t next_id(void) { return _dot_id++; }

// Escape double-quotes so Graphviz labels don't break.
static void fprint_escaped(FILE *f, const char *s)
{
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        fputc(*s, f);
    }
}

// Emit a directed edge between two node-ids.
static void dot_edge(FILE *f, size_t from, size_t to, const char *label)
{
    fprintf(f, "  n%zu -> n%zu", from, to);
    if (label && label[0]) fprintf(f, " [label=\"%s\"]", label);
    fputs(";\n", f);
}

// Emit a labelled, coloured box node.
static void dot_node(FILE *f, size_t id, const char *title, const char *body, const char *color)
{
    fprintf(f, "  n%zu [label=\"{%s", id, title);
    if (body && body[0]) {
        fputs("|", f);
        fprint_escaped(f, body);
    }
    fprintf(f, "}\", style=filled, fillcolor=\"%s\"];\n", color ? color : "white");
}

// Build a one-line description of a TypeInfo (used in node labels).
static void sprint_type(char *buf, size_t bufsz, TypeInfo *t)
{
    if (!t) { snprintf(buf, bufsz, "(null)"); return; }
    switch (t->kind) {
    case KIND_NOT_DECLARED: snprintf(buf, bufsz, "?not-declared");          break;
    case KIND_NOT_INFERRED: snprintf(buf, bufsz, "?not-inferred");          break;
    case KIND_PRIMITIVE:    snprintf(buf, bufsz, "%s", primitive_type_as_string(t->primitive)); break;
    case KIND_LIST:         snprintf(buf, bufsz, "[...]");                  break;
    case KIND_FUNCTION:     snprintf(buf, bufsz, "fn(%zu params)", t->fn.params.count); break;
    case KIND_STRUCT:       snprintf(buf, bufsz, "struct %s", t->name);     break;
    default:                snprintf(buf, bufsz, "kind=%d", t->kind);       break;
    }
}

static size_t dump_node(FILE *f, ASTNode *node);

// Emit a whole linked-list chain and return id of the first node.
// Each node in the chain is connected to the next via a "next" edge.
static size_t dump_list(FILE *f, ASTNode *node)
{
    if (!node) return (size_t)-1;
    size_t first = dump_node(f, node);
    size_t prev  = first;
    node = node->next;
    while (node) {
        size_t cur = dump_node(f, node);
        dot_edge(f, prev, cur, "next");
        prev = cur;
        node = node->next;
    }
    return first;
}

// Returns the Graphviz id of the emitted node.
static size_t dump_node(FILE *f, ASTNode *node)
{
    if (!node) return (size_t)-1;

    size_t id = next_id();
    char body[256] = {0};
    char type_buf[128];

    static_assert(__astnode_types_count == 18, "Cover all ASTNodeTypes in dump_node");

    switch (node->type) {

    case ASTNODE_PROGRAM: {
        dot_node(f, id, "PROGRAM", "", "#AED6F1");
        if (node->next) {
            size_t child = dump_list(f, node->next);
            if (child != (size_t)-1) dot_edge(f, id, child, "body");
        }
    } break;

    case ASTNODE_DECLARATION: {
        char sym_kind[32] = "";
        if (node->sym) {
            switch (node->sym->kind) {
            case SYM_VARIABLE: strcpy(sym_kind, "var");  break;
            case SYM_FUNCTION: strcpy(sym_kind, "fn");   break;
            case SYM_TYPE:     strcpy(sym_kind, "type"); break;
            default:           strcpy(sym_kind, "?");    break;
            }
            sprint_type(type_buf, sizeof type_buf, node->sym->type);
            snprintf(body, sizeof body, "%s %s : %s", sym_kind, node->name, type_buf);
        } else {
            snprintf(body, sizeof body, "%s (unresolved)", node->name);
        }
        dot_node(f, id, "DECLARATION", body, "#A9DFBF");

        // function body block
        if (node->decl.fn.block) {
            size_t blk = dump_node(f, node->decl.fn.block);
            dot_edge(f, id, blk, "body");
        }
        // variable initialiser expression
        if (node->decl.var.expr && node->decl.fn.block == NULL) {
            size_t expr = dump_node(f, node->decl.var.expr);
            dot_edge(f, id, expr, "init");
        }
        // function parameters (via sym->fn.params)
        if (node->sym && node->sym->kind == SYM_FUNCTION) {
            for (size_t i = 0; i < node->sym->fn.params.count; i++) {
                Symbol *p = node->sym->fn.params.items[i];
                size_t pid = next_id();
                sprint_type(type_buf, sizeof type_buf, p->type);
                snprintf(body, sizeof body, "%s : %s", p->name, type_buf);
                dot_node(f, pid, "PARAM", body, "#D5F5E3");
                dot_edge(f, id, pid, "param");
            }
        }
    } break;

    case ASTNODE_ASSIGNMENT: {
        dot_node(f, id, "ASSIGNMENT", "", "#FAD7A0");
        if (node->assign.lhs) {
            size_t lhs = dump_node(f, node->assign.lhs);
            dot_edge(f, id, lhs, "lhs");
        }
        if (node->assign.rhs) {
            size_t rhs = dump_node(f, node->assign.rhs);
            dot_edge(f, id, rhs, "rhs");
        }
    } break;

    case ASTNODE_BLOCK: {
        dot_node(f, id, "BLOCK", "", "#D7BDE2");
        if (node->list) {
            size_t child = dump_list(f, node->list);
            if (child != (size_t)-1) dot_edge(f, id, child, "stmts");
        }
    } break;

    case ASTNODE_IF: {
        dot_node(f, id, "IF", "", "#F9E79F");
        if (node->conditional.expr) {
            size_t cond = dump_node(f, node->conditional.expr);
            dot_edge(f, id, cond, "cond");
        }
        if (node->conditional.block) {
            size_t blk = dump_node(f, node->conditional.block);
            dot_edge(f, id, blk, "then");
        }
        if (node->conditional.list) {
            size_t rest = dump_list(f, node->conditional.list);
            if (rest != (size_t)-1) dot_edge(f, id, rest, "elif/else");
        }
    } break;

    case ASTNODE_ELIF: {
        dot_node(f, id, "ELIF", "", "#FCF3CF");
        if (node->conditional.expr) {
            size_t cond = dump_node(f, node->conditional.expr);
            dot_edge(f, id, cond, "cond");
        }
        if (node->conditional.block) {
            size_t blk = dump_node(f, node->conditional.block);
            dot_edge(f, id, blk, "then");
        }
        if (node->conditional.list) {
            size_t rest = dump_node(f, node->conditional.list);
            dot_edge(f, id, rest, "elif/else");
        }
    } break;

    case ASTNODE_ELSE: {
        dot_node(f, id, "ELSE", "", "#FDEBD0");
        if (node->conditional.block) {
            size_t blk = dump_node(f, node->conditional.block);
            dot_edge(f, id, blk, "body");
        }
    } break;

    case ASTNODE_WHILE: {
        dot_node(f, id, "WHILE", "", "#F0B27A");
        if (node->conditional.expr) {
            size_t cond = dump_node(f, node->conditional.expr);
            dot_edge(f, id, cond, "cond");
        }
        if (node->conditional.block) {
            size_t blk = dump_node(f, node->conditional.block);
            dot_edge(f, id, blk, "body");
        }
    } break;

    case ASTNODE_FOR: {
        dot_node(f, id, "FOR", "(TODO)", "#F0B27A");
        if (node->conditional.expr) {
            size_t cond = dump_node(f, node->conditional.expr);
            dot_edge(f, id, cond, "cond");
        }
        if (node->conditional.block) {
            size_t blk = dump_node(f, node->conditional.block);
            dot_edge(f, id, blk, "body");
        }
    } break;

    case ASTNODE_RETURN: {
        dot_node(f, id, "RETURN", "", "#AED6F1");
        if (node->ret.expr) {
            size_t expr = dump_node(f, node->ret.expr);
            dot_edge(f, id, expr, "expr");
        }
    } break;

    case ASTNODE_CALL:
    case ASTNODE_CALL_STATEMENT: {
        const char *title = (node->type == ASTNODE_CALL) ? "CALL" : "CALL_STMT";
        snprintf(body, sizeof body, "%s()", node->name);
        dot_node(f, id, title, body, "#85C1E9");

        if (node->call.callee) {
            size_t callee = dump_node(f, node->call.callee);
            dot_edge(f, id, callee, "callee");
        }
        // args is a linked list of expression nodes
        if (node->call.args) {
            size_t arg_chain = dump_list(f, node->call.args);
            if (arg_chain != (size_t)-1) dot_edge(f, id, arg_chain, "args");
        }
    } break;

    case ASTNODE_FIELD_ACCESS: {
        snprintf(body, sizeof body, ".%s", node->name);
        dot_node(f, id, "FIELD_ACCESS", body, "#D2B4DE");
        if (node->accessed_struct) {
            size_t strct = dump_node(f, node->accessed_struct);
            dot_edge(f, id, strct, "struct");
        }
    } break;

    case ASTNODE_BINOP: {
        snprintf(body, sizeof body, "%s", operator_as_string(node->binary.op));
        dot_node(f, id, "BINOP", body, "#F7DC6F");
        if (node->binary.left) {
            size_t lhs = dump_node(f, node->binary.left);
            dot_edge(f, id, lhs, "lhs");
        }
        if (node->binary.right) {
            size_t rhs = dump_node(f, node->binary.right);
            dot_edge(f, id, rhs, "rhs");
        }
    } break;

    case ASTNODE_NUMBER: {
        snprintf(body, sizeof body, "%d", node->value._int);
        dot_node(f, id, "NUMBER", body, "#A9CCE3");
    } break;

    case ASTNODE_STRING: {
        snprintf(body, sizeof body, "\\\"%s\\\"", node->value.string);
        dot_node(f, id, "STRING", body, "#A9CCE3");
    } break;

    case ASTNODE_BOOL: {
        dot_node(f, id, "BOOL", node->value._bool ? "true" : "false", "#A9CCE3");
    } break;

    case ASTNODE_IDENT: {
        if (node->sym) {
            sprint_type(type_buf, sizeof type_buf, node->sym->type);
            snprintf(body, sizeof body, "%s : %s", node->name, type_buf);
        } else {
            snprintf(body, sizeof body, "%s", node->name);
        }
        dot_node(f, id, "IDENT", body, "#D5DBDB");
    } break;

    default: {
        snprintf(body, sizeof body, "type=%d", node->type);
        dot_node(f, id, "UNKNOWN", body, "#E74C3C");
    } break;
    }

    return id;
}

void dump_ast(ASTNode *root)
{
    const char *filename = "ast.dot";
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "dump_ast: could not open '%s'\n", filename);
        return;
    }

    _dot_id = 0; // reset id counter

    fputs("digraph AST {\n", f);
    fputs("  graph [rankdir=TB, fontname=\"Monospace\"];\n", f);
    fputs("  node  [shape=Mrecord, fontname=\"Monospace\", fontsize=11];\n", f);
    fputs("  edge  [fontname=\"Monospace\", fontsize=9];\n\n", f);

    if (root) dump_node(f, root);

    fputs("}\n", f);
    fclose(f);

    printf("AST written to '%s'. Render with:\n"
           "  dot -Tsvg %s -o ast.svg\n"
           "  dot -Tpng %s -o ast.png\n",
           filename, filename, filename);
}
