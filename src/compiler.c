#include "compiler.h"
#include "scanner.h"
#include "common.h"
#include "object.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef LUNAR_DEBUG_PRINT_CODE
#include "debug.h"
#endif

/* ═══════════════════════════════════════════
   Parser
   ═══════════════════════════════════════════ */

typedef struct {
    Token curr;
    Token prev;
    bool had_err;
    bool panic_mode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // ||
    PREC_AND,         // &&
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * / %
    PREC_POWER,       // **
    PREC_BIT_AND,     // &
    PREC_BIT_XOR,     // ^
    PREC_BIT_OR,      // |
    PREC_SHIFT,       // << >>
    PREC_UNARY,       // ! - ~
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool can_assign);

typedef struct {
    ParseFn    prefix;
    ParseFn    infix;
    Precedence precedence;
} ParseRule;

/* ═══════════════════════════════════════════
   Local variable tracking
   ═══════════════════════════════════════════ */

typedef struct {
    Token name;
    int   depth;
    bool  is_captured;  // captured by a closure?
} Local;

typedef struct {
    u8   index;
    bool is_local;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_METHOD,
    TYPE_INITIALIZER,
    TYPE_SCRIPT,
} FunctionType;

/* ═══════════════════════════════════════════
   Compiler & Class Compiler
   ═══════════════════════════════════════════ */

#define MAX_LOCALS   256
#define MAX_UPVALUES 256

typedef struct Compiler {
    struct Compiler* enclosing;

    ObjFunction* function;
    FunctionType type;

    Local   locals[MAX_LOCALS];
    int     local_count;
    Upvalue upvalues[MAX_UPVALUES];
    int     scope_depth;
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
    bool has_superclass;
} ClassCompiler;

/* ═══════════════════════════════════════════
   Globals
   ═══════════════════════════════════════════ */

Parser        parser;
Compiler*     current     = NULL;
ClassCompiler* current_class = NULL;

/* ═══════════════════════════════════════════
   Forward declarations
   ═══════════════════════════════════════════ */

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static u8   identifier_constant(Token* name);
static int  resolve_local(Compiler* compiler, Token* name);
static int  resolve_upvalue(Compiler* compiler, Token* name);
static void and_(bool can_assign);
static void or_(bool can_assign);
static void named_variable(Token name, bool can_assign);
static void variable(bool can_assign);
static void this_(bool can_assign);
static void super_(bool can_assign);
static Token synthetic_token(const char* text);
static void fn_declaration();
static void function(FunctionType type);
static void method();
static void struct_declaration();
static void block();

/* ═══════════════════════════════════════════
   Error helpers
   ═══════════════════════════════════════════ */

static void error_at(Token* tok, const char* msg) {
    if (parser.panic_mode) return;
    parser.panic_mode = true;

    fprintf(stderr, "[line %d] ERROR", tok->line);

    if      (TOKEN_EOF   == tok->type) fprintf(stderr, " at end");
    else if (TOKEN_ERROR == tok->type) { /* nothing */ }
    else fprintf(stderr, " at '%.*s'", tok->length, tok->start);

    fprintf(stderr, ": %s\n", msg);
    parser.had_err = true;
}

static void error(const char* msg)            { error_at(&parser.prev, msg); }
static void error_at_current(const char* msg) { error_at(&parser.curr, msg); }

/* ═══════════════════════════════════════════
   Parser primitives
   ═══════════════════════════════════════════ */

static void advance() {
    parser.prev = parser.curr;
    for (;;) {
        parser.curr = scan_token();
        if (TOKEN_ERROR != parser.curr.type) break;
        error_at_current(parser.curr.start);
    }
}

static void consume(TokenType ty, const char* msg) {
    if (parser.curr.type == ty) { advance(); return; }
    error_at_current(msg);
}

static bool check(TokenType ty) { return parser.curr.type == ty; }

static bool match(TokenType ty) {
    if (!check(ty)) return false;
    advance();
    return true;
}

/* ═══════════════════════════════════════════
   Chunk helpers
   ═══════════════════════════════════════════ */

static Chunk* current_chunk() { return &current->function->chunk; }

static void emit_byte(u8 byte) {
    write_chunk(current_chunk(), byte, parser.prev.line);
}

static void emit_bytes(u8 b1, u8 b2) { emit_byte(b1); emit_byte(b2); }

static void emit_return() {
    if (current->type == TYPE_INITIALIZER) {
        emit_bytes(OP_GET_LOCAL, 0); // return 'self'
    } else {
        emit_byte(OP_NIL);
    }
    emit_byte(OP_RET);
}

static u8 make_const(Value val) {
    int c = add_constant(current_chunk(), val);
    if (c > UINT8_MAX) { error("Too many constants in one chunk."); return 0; }
    return (u8)c;
}

static void emit_const(Value value) { emit_bytes(OP_CONST, make_const(value)); }

/* ── Jump patching ── */
static int emit_jump(u8 instruction) {
    emit_byte(instruction);
    emit_byte(0xff);
    emit_byte(0xff);
    return current_chunk()->count - 2;
}

static void patch_jump(int offset) {
    int jump = current_chunk()->count - offset - 2;
    if (jump > UINT16_MAX) error("Too much code to jump over.");
    current_chunk()->code[offset]     = (jump >> 8) & 0xff;
    current_chunk()->code[offset + 1] =  jump       & 0xff;
}

static void emit_loop(int loop_start) {
    emit_byte(OP_LOOP);
    int offset = current_chunk()->count - loop_start + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
    emit_byte((offset >> 8) & 0xff);
    emit_byte( offset       & 0xff);
}

/* ═══════════════════════════════════════════
   Compiler init / end
   ═══════════════════════════════════════════ */

static void init_compiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing   = current;
    compiler->function    = NULL;
    compiler->type        = type;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->function    = new_function();
    current               = compiler;

    if (type != TYPE_SCRIPT) {
        current->function->name = copy_str(parser.prev.start, parser.prev.length);
    }

    // claim slot 0 for 'self' in methods, or empty in functions
    Local* local        = &current->locals[current->local_count++];
    local->depth        = 0;
    local->is_captured  = false;
    if (type != TYPE_FUNCTION) {
        local->name.start  = "self";
        local->name.length = 4;
    } else {
        local->name.start  = "";
        local->name.length = 0;
    }
}

static ObjFunction* end_compiler() {
    emit_return();
    ObjFunction* fn = current->function;

#ifdef LUNAR_DEBUG_PRINT_CODE
    if (!parser.had_err) {
        disassemble_chunk(current_chunk(),
            fn->name != NULL ? fn->name->chars : "<script>");
    }
#endif

    current = current->enclosing;
    return fn;
}

/* ═══════════════════════════════════════════
   Scope management
   ═══════════════════════════════════════════ */

static void begin_scope() { current->scope_depth++; }

static void end_scope() {
    current->scope_depth--;
    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth)
    {
        if (current->locals[current->local_count - 1].is_captured) {
            emit_byte(OP_CLOSE_UPVALUE);
        } else {
            emit_byte(OP_POP);
        }
        current->local_count--;
    }
}

/* ═══════════════════════════════════════════
   Variable resolution
   ═══════════════════════════════════════════ */

static bool identifiers_equal(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolve_local(Compiler* compiler, Token* name) {
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiers_equal(name, &local->name)) {
            if (local->depth == -1) error("Can't read local variable in its own initializer.");
            return i;
        }
    }
    return -1;
}

static int add_upvalue(Compiler* compiler, u8 index, bool is_local) {
    int count = compiler->function->upvalue_count;

    for (int i = 0; i < count; i++) {
        Upvalue* uv = &compiler->upvalues[i];
        if (uv->index == index && uv->is_local == is_local) return i;
    }

    if (count == MAX_UPVALUES) { error("Too many closure variables in function."); return 0; }

    compiler->upvalues[count].is_local = is_local;
    compiler->upvalues[count].index    = index;
    return compiler->function->upvalue_count++;
}

static int resolve_upvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolve_local(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (u8)local, true);
    }

    int upvalue = resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1) return add_upvalue(compiler, (u8)upvalue, false);

    return -1;
}

static void add_local(Token name) {
    if (current->local_count == MAX_LOCALS) {
        error("Too many local variables in function.");
        return;
    }
    Local* local       = &current->locals[current->local_count++];
    local->name        = name;
    local->depth       = -1; // mark as uninitialized
    local->is_captured = false;
}

static void declare_variable() {
    if (current->scope_depth == 0) return; // global

    Token* name = &parser.prev;
    for (int i = current->local_count - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scope_depth) break;
        if (identifiers_equal(name, &local->name))
            error("Already a variable with this name in this scope.");
    }
    add_local(*name);
}

static u8 parse_variable(const char* err_msg) {
    consume(TOKEN_IDENTIFIER, err_msg);
    declare_variable();
    if (current->scope_depth > 0) return 0; // locals use slot, not constant
    return identifier_constant(&parser.prev);
}

static void mark_initialized() {
    if (current->scope_depth == 0) return;
    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(u8 global) {
    if (current->scope_depth > 0) { mark_initialized(); return; }
    emit_bytes(OP_DEFINE_GLOBAL, global);
}

static u8 identifier_constant(Token* name) {
    return make_const(OBJ_VAL(copy_str(name->start, name->length)));
}

static u8 argument_list() {
    u8 argc = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argc == 255) error("Can't have more than 255 arguments.");
            argc++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argc;
}

/* ═══════════════════════════════════════════
   Parse functions (Pratt table entries)
   ═══════════════════════════════════════════ */

static void number(bool can_assign) {
    double val = strtod(parser.prev.start, NULL);
    emit_const(NUMBER_VAL(val));
}

static void string_(bool can_assign) {
    emit_const(OBJ_VAL(copy_str(parser.prev.start + 1, parser.prev.length - 2)));
}

static void literal(bool can_assign) {
    switch (parser.prev.type) {
        case TOKEN_FALSE: emit_byte(OP_FALSE); break;
        case TOKEN_TRUE:  emit_byte(OP_TRUE);  break;
        case TOKEN_NIL:   emit_byte(OP_NIL);   break;
        default: return;
    }
}

static void grouping(bool can_assign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void unary(bool can_assign) {
    TokenType op = parser.prev.type;
    parsePrecedence(PREC_UNARY);
    switch (op) {
        case TOKEN_MINUS:       emit_byte(OP_NEGATE);      break;
        case TOKEN_BANG:        emit_byte(OP_NOT);         break;
        case TOKEN_BITWISE_NOT: emit_byte(OP_BITWISE_NOT); break;
        default: return;
    }
}

static void binary(bool can_assign) {
    TokenType  op   = parser.prev.type;
    ParseRule* rule = getRule(op);

    // ** is right-associative
    parsePrecedence((Precedence)(
        op == TOKEN_STAR_STAR ? rule->precedence : rule->precedence + 1
    ));

    switch (op) {
        case TOKEN_PLUS:                emit_byte(OP_ADD);                  break;
        case TOKEN_MINUS:               emit_byte(OP_SUB);                  break;
        case TOKEN_STAR:                emit_byte(OP_MUL);                  break;
        case TOKEN_SLASH:               emit_byte(OP_DIV);                  break;
        case TOKEN_STAR_STAR:           emit_byte(OP_POW);                  break;
        case TOKEN_PERCENT:             emit_byte(OP_MOD);                  break;
        case TOKEN_BITWISE_AND:         emit_byte(OP_BITWISE_AND);          break;
        case TOKEN_BITWISE_OR:          emit_byte(OP_BITWISE_OR);           break;
        case TOKEN_BITWISE_XOR:         emit_byte(OP_BITWISE_XOR);          break;
        case TOKEN_BITWISE_LEFT_SHIFT:  emit_byte(OP_BITWISE_LEFTSHIFT);    break;
        case TOKEN_BITWISE_RIGHT_SHIFT: emit_byte(OP_BITWISE_RIGHTSHIFT);   break;
        case TOKEN_BANG_EQUAL:          emit_bytes(OP_EQU, OP_NOT);         break;
        case TOKEN_EQUAL_EQUAL:         emit_byte(OP_EQU);                  break;
        case TOKEN_GREATER:             emit_byte(OP_GREATER);              break;
        case TOKEN_GREATER_EQUAL:       emit_bytes(OP_LESS, OP_NOT);        break;
        case TOKEN_LESS:                emit_byte(OP_LESS);                 break;
        case TOKEN_LESS_EQUAL:          emit_bytes(OP_GREATER, OP_NOT);     break;
        default: return;
    }
}

static void call_(bool can_assign) {
    u8 argc = argument_list();
    emit_bytes(OP_CALL, argc);
}

static void dot(bool can_assign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    u8 name = identifier_constant(&parser.prev);

    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_bytes(OP_SET_PROPERTY, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        // optimised method invoke
        u8 argc = argument_list();
        emit_byte(OP_INVOKE);
        emit_byte(name);
        emit_byte(argc);
    } else {
        emit_bytes(OP_GET_PROPERTY, name);
    }
}

static void and_(bool can_assign) {
    int end_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    parsePrecedence(PREC_AND);
    patch_jump(end_jump);
}

static void or_(bool can_assign) {
    int else_jump = emit_jump(OP_JUMP_IF_FALSE);
    int end_jump  = emit_jump(OP_JUMP);
    patch_jump(else_jump);
    emit_byte(OP_POP);
    parsePrecedence(PREC_OR);
    patch_jump(end_jump);
}

static void named_variable(Token name, bool can_assign) {
    u8 get_op, set_op;
    int arg = resolve_local(current, &name);

    if (arg != -1) {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
    } else if ((arg = resolve_upvalue(current, &name)) != -1) {
        get_op = OP_GET_UPVALUE;
        set_op = OP_SET_UPVALUE;
    } else {
        arg    = identifier_constant(&name);
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
    }

    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_bytes(set_op, (u8)arg);
    } else {
        emit_bytes(get_op, (u8)arg);
    }
}

static void variable(bool can_assign) {
    named_variable(parser.prev, can_assign);
}

static Token synthetic_token(const char* text) {
    Token tok;
    tok.start  = text;
    tok.length = (int)strlen(text);
    return tok;
}

static void this_(bool can_assign) {
    if (current_class == NULL) { error("Can't use 'self' outside of a struct."); return; }
    variable(false);
}

static void super_(bool can_assign) {
    if (current_class == NULL)              error("Can't use 'super' outside of a struct.");
    else if (!current_class->has_superclass) error("Can't use 'super' in a struct with no supertype.");

    consume(TOKEN_DOT,        "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect supertype method name.");
    u8 name = identifier_constant(&parser.prev);

    named_variable(synthetic_token("self"),  false);

    if (match(TOKEN_LEFT_PAREN)) {
        u8 argc = argument_list();
        named_variable(synthetic_token("super"), false);
        emit_byte(OP_SUPER_INVOKE);
        emit_byte(name);
        emit_byte(argc);
    } else {
        named_variable(synthetic_token("super"), false);
        emit_bytes(OP_GET_SUPER, name);
    }
}

/* ═══════════════════════════════════════════
   Parse rule table
   ═══════════════════════════════════════════ */

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]          = {grouping,  call_,   PREC_CALL},
    [TOKEN_RIGHT_PAREN]         = {NULL,       NULL,    PREC_NONE},
    [TOKEN_LEFT_BRACE]          = {NULL,       NULL,    PREC_NONE},
    [TOKEN_RIGHT_BRACE]         = {NULL,       NULL,    PREC_NONE},
    [TOKEN_DOT]                 = {NULL,       dot,     PREC_CALL},
    [TOKEN_MINUS]               = {unary,      binary,  PREC_TERM},
    [TOKEN_PLUS]                = {NULL,       binary,  PREC_TERM},
    [TOKEN_SEMICOLON]           = {NULL,       NULL,    PREC_NONE},
    [TOKEN_SLASH]               = {NULL,       binary,  PREC_FACTOR},
    [TOKEN_STAR]                = {NULL,       binary,  PREC_FACTOR},
    [TOKEN_STAR_STAR]           = {NULL,       binary,  PREC_POWER},
    [TOKEN_PERCENT]             = {NULL,       binary,  PREC_FACTOR},
    [TOKEN_BITWISE_NOT]         = {unary,      NULL,    PREC_UNARY},
    [TOKEN_BITWISE_OR]          = {NULL,       binary,  PREC_BIT_OR},
    [TOKEN_BITWISE_XOR]         = {NULL,       binary,  PREC_BIT_XOR},
    [TOKEN_BITWISE_AND]         = {NULL,       binary,  PREC_BIT_AND},
    [TOKEN_BITWISE_LEFT_SHIFT]  = {NULL,       binary,  PREC_SHIFT},
    [TOKEN_BITWISE_RIGHT_SHIFT] = {NULL,       binary,  PREC_SHIFT},
    [TOKEN_BANG]                = {unary,      NULL,    PREC_NONE},
    [TOKEN_BANG_EQUAL]          = {NULL,       binary,  PREC_EQUALITY},
    [TOKEN_EQUAL]               = {NULL,       NULL,    PREC_NONE},
    [TOKEN_EQUAL_EQUAL]         = {NULL,       binary,  PREC_EQUALITY},
    [TOKEN_GREATER]             = {NULL,       binary,  PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL]       = {NULL,       binary,  PREC_COMPARISON},
    [TOKEN_LESS]                = {NULL,       binary,  PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]          = {NULL,       binary,  PREC_COMPARISON},
    [TOKEN_IDENTIFIER]          = {variable,   NULL,    PREC_NONE},
    [TOKEN_STRING]              = {string_,    NULL,    PREC_NONE},
    [TOKEN_NUMBER]              = {number,     NULL,    PREC_NONE},
    [TOKEN_AND]                 = {NULL,       and_,    PREC_AND},
    [TOKEN_OR]                  = {NULL,       or_,     PREC_OR},
    [TOKEN_ELSE]                = {NULL,       NULL,    PREC_NONE},
    [TOKEN_FALSE]               = {literal,    NULL,    PREC_NONE},
    [TOKEN_FOR]                 = {NULL,       NULL,    PREC_NONE},
    [TOKEN_FN]                  = {NULL,       NULL,    PREC_NONE},
    [TOKEN_IF]                  = {NULL,       NULL,    PREC_NONE},
    [TOKEN_NIL]                 = {literal,    NULL,    PREC_NONE},
    [TOKEN_PRINT]               = {NULL,       NULL,    PREC_NONE},
    [TOKEN_STRUCT]              = {NULL,       NULL,    PREC_NONE},
    [TOKEN_SELF]                = {this_,      NULL,    PREC_NONE},
    [TOKEN_SUPER]               = {super_,     NULL,    PREC_NONE},
    [TOKEN_RET]                 = {NULL,       NULL,    PREC_NONE},
    [TOKEN_TRUE]                = {literal,    NULL,    PREC_NONE},
    [TOKEN_LET]                 = {NULL,       NULL,    PREC_NONE},
    [TOKEN_WHILE]               = {NULL,       NULL,    PREC_NONE},
    [TOKEN_ERROR]               = {NULL,       NULL,    PREC_NONE},
    [TOKEN_EOF]                 = {NULL,       NULL,    PREC_NONE},
};

static ParseRule* getRule(TokenType type) { return &rules[type]; }

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefix = getRule(parser.prev.type)->prefix;
    if (prefix == NULL) { error("Expected expression."); return; }

    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix(can_assign);

    while (precedence <= getRule(parser.curr.type)->precedence) {
        advance();
        ParseFn infix = getRule(parser.prev.type)->infix;
        if (infix == NULL) { error("Invalid binary expression."); return; }
        infix(can_assign);
    }

    if (can_assign && match(TOKEN_EQUAL)) error("Invalid assignment target.");
}

static void expression() { parsePrecedence(PREC_ASSIGNMENT); }

/* ═══════════════════════════════════════════
   Statements
   ═══════════════════════════════════════════ */

static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void print_statement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emit_byte(OP_PRINT);
}

static void expression_statement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emit_byte(OP_POP);
}

static void if_statement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int then_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();

    int else_jump = emit_jump(OP_JUMP);
    patch_jump(then_jump);
    emit_byte(OP_POP);

    if (match(TOKEN_ELSE)) statement();
    patch_jump(else_jump);
}

static void while_statement() {
    int loop_start = current_chunk()->count;

    consume(TOKEN_LEFT_PAREN,  "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exit_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();
    emit_loop(loop_start);

    patch_jump(exit_jump);
    emit_byte(OP_POP);
}

static void for_statement() {
    begin_scope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // initializer
    if (match(TOKEN_SEMICOLON)) {
        // no initializer
    } else if (match(TOKEN_LET)) {
        // variable declaration as initializer
        u8 global = parse_variable("Expect variable name.");
        if (match(TOKEN_EQUAL)) expression(); else emit_byte(OP_NIL);
        consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
        define_variable(global);
    } else {
        expression_statement();
    }

    int loop_start = current_chunk()->count;
    int exit_jump  = -1;

    // condition
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
        exit_jump = emit_jump(OP_JUMP_IF_FALSE);
        emit_byte(OP_POP);
    }

    // increment (compile now, jump over it, run after body)
    if (!match(TOKEN_RIGHT_PAREN)) {
        int body_jump      = emit_jump(OP_JUMP);
        int increment_start = current_chunk()->count;
        expression();
        emit_byte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
        emit_loop(loop_start);
        loop_start = increment_start;
        patch_jump(body_jump);
    }

    statement();
    emit_loop(loop_start);

    if (exit_jump != -1) {
        patch_jump(exit_jump);
        emit_byte(OP_POP);
    }

    end_scope();
}

static void return_statement() {
    if (current->type == TYPE_SCRIPT) error("Can't return from top-level code.");

    if (match(TOKEN_SEMICOLON)) {
        emit_return();
    } else {
        if (current->type == TYPE_INITIALIZER)
            error("Can't return a value from an initializer.");
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emit_byte(OP_RET);
    }
}

static void statement() {
    if (match(TOKEN_PRINT)) {
        print_statement();
    } else if (match(TOKEN_IF)) {
        if_statement();
    } else if (match(TOKEN_WHILE)) {
        while_statement();
    } else if (match(TOKEN_FOR)) {
        for_statement();
    } else if (match(TOKEN_RET)) {
        return_statement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        begin_scope();
        block();
        end_scope();
    } else {
        expression_statement();
    }
}

/* ═══════════════════════════════════════════
   Declarations
   ═══════════════════════════════════════════ */

static void function(FunctionType type) {
    Compiler compiler;
    init_compiler(&compiler, type);
    begin_scope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) error_at_current("Can't have more than 255 parameters.");
            u8 constant = parse_variable("Expect parameter name.");
            define_variable(constant);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE,  "Expect '{' before function body.");
    block();

    ObjFunction* fn = end_compiler();
    emit_bytes(OP_CLOSURE, make_const(OBJ_VAL(fn)));

    // emit upvalue descriptors
    for (int i = 0; i < fn->upvalue_count; i++) {
        emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler.upvalues[i].index);
    }
}

static void fn_declaration() {
    u8 global = parse_variable("Expect function name.");
    mark_initialized();
    function(TYPE_FUNCTION);
    define_variable(global);
}

static void let_declaration() {
    u8 global = parse_variable("Expect variable name.");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emit_byte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    define_variable(global);
}

static void method() {
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    u8 constant = identifier_constant(&parser.prev);

    FunctionType type = TYPE_METHOD;
    if (parser.prev.length == 4 && memcmp(parser.prev.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }

    function(type);
    emit_bytes(OP_METHOD, constant);
}

static void struct_declaration() {
    consume(TOKEN_IDENTIFIER, "Expect struct name.");
    Token struct_name = parser.prev;
    u8    name_const  = identifier_constant(&parser.prev);

    declare_variable();
    emit_bytes(OP_STRUCT, name_const);
    define_variable(name_const);

    ClassCompiler class_compiler;
    class_compiler.enclosing     = current_class;
    class_compiler.has_superclass = false;
    current_class = &class_compiler;

    // optional inheritance:  struct Foo < Bar { ... }
    if (match(TOKEN_LESS)) {
        consume(TOKEN_IDENTIFIER, "Expect supertype name.");
        variable(false); // push supertype

        if (identifiers_equal(&struct_name, &parser.prev))
            error("A struct can't inherit from itself.");

        begin_scope();
        add_local(synthetic_token("super"));
        define_variable(0);

        named_variable(struct_name, false);
        emit_byte(OP_INHERIT);
        class_compiler.has_superclass = true;
    }

    named_variable(struct_name, false); // push struct for OP_METHOD
    consume(TOKEN_LEFT_BRACE, "Expect '{' before struct body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        method();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after struct body.");
    emit_byte(OP_POP); // pop struct

    if (class_compiler.has_superclass) end_scope();

    current_class = current_class->enclosing;
}

static void synchronize() {
    parser.panic_mode = false;
    while (parser.curr.type != TOKEN_EOF) {
        if (parser.prev.type == TOKEN_SEMICOLON) return;
        switch (parser.curr.type) {
            case TOKEN_STRUCT: case TOKEN_FN: case TOKEN_LET:
            case TOKEN_FOR: case TOKEN_IF: case TOKEN_WHILE:
            case TOKEN_PRINT: case TOKEN_RET:
                return;
            default:;
        }
        advance();
    }
}

static void declaration() {
    if (match(TOKEN_STRUCT)) {
        struct_declaration();
    } else if (match(TOKEN_FN)) {
        fn_declaration();
    } else if (match(TOKEN_LET)) {
        let_declaration();
    } else {
        statement();
    }
    if (parser.panic_mode) synchronize();
}

/* ═══════════════════════════════════════════
   Entry points
   ═══════════════════════════════════════════ */

ObjFunction* compile(const char* src) {
    init_scanner(src);

    Compiler compiler;
    init_compiler(&compiler, TYPE_SCRIPT);

    parser.had_err    = false;
    parser.panic_mode = false;

    advance();
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction* fn = end_compiler();
    return parser.had_err ? NULL : fn;
}

void mark_compiler_roots() {
    Compiler* compiler = current;
    while (compiler != NULL) {
        mark_object((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
