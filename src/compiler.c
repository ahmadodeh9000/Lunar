#include "compiler.h"
#include "scanner.h"
#include "common.h"
#include "object.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifdef LUNAR_DEBUG_PRINT_CODE
#include "debug.h"
#endif


// parser

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
  PREC_FACTOR,      // * /

  PREC_POWER,       // **


  PREC_BIT_AND,     // &
  PREC_BIT_XOR,     // ^
  PREC_BIT_OR,      // |

  PREC_SHIFT,       // << >>


  PREC_UNARY,       // ! - ~
  PREC_CALL,        // ()
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)();

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;





// global variables 
Parser parser;
Chunk* compiling_chunk;


// errors functions

static void error_at_current(const char* err_msg);
static void error_at(Token* tok, const char* msg);
static void error(const char* msg);


static void error(const char* msg) {
    error_at(&parser.prev, msg);
}


static void error_at(Token* tok, const char* msg) {

    if (parser.panic_mode) return;

    parser.panic_mode = true;
    
    fprintf(stderr,"[line %d] ERROR ", tok->line);

    if (TOKEN_EOF == tok->type) {
        fprintf(stderr, " at the end ! ");
    }

    else if (TOKEN_ERROR == tok->type) {
        /**/
    }
    else {
        fprintf(stderr, " at '%.*s'", tok->length, tok->start);
    }

    fprintf(stderr, ": %s\n",msg);
    parser.had_err = true;

}

static void error_at_current(const char* err_msg) {
    error_at(&parser.curr,err_msg);;
}






// parser helpers functions defs
static void advance();
static void consume(TokenType ty, const char *err_msg);

// parser functions
static void advance() {
    parser.prev = parser.curr;


    for (;;) {
        parser.curr = scan_token();

        if (TOKEN_ERROR != parser.curr.type) break;
        error_at_current(parser.curr.start);

    }
}

static void consume(TokenType ty, const char *err_msg) {
    if (parser.curr.type == ty) {
        advance();
        return;
    }

    error_at_current(err_msg);
}

// chunk helpers functions defs
static Chunk* current_chunk();
static void end_compiler();
static void emit_byte(u8 byte);
static void emit_bytes(u8 byte1, u8 byte2);
static void emit_const(Value value);
static void emit_return();
static void number();
static u8 make_const(Value val);
static void grouping();
static void unary();
static void binary();

static void parsePrecedence(Precedence precedence);
static void expression();
static ParseRule* getRule(TokenType type);

static void literal();
static void string();


static void string() {
  emit_const(OBJ_VAL(copy_str(parser.prev.start + 1,
                                  parser.prev.length - 2)));
}

static void literal() {
    switch(parser.prev.type) {
        case TOKEN_FALSE: emit_byte(OP_FALSE); break;
        case TOKEN_TRUE: emit_byte(OP_TRUE); break;
        case TOKEN_NIL: emit_byte(OP_NIL); break;

        default: return;
    }
}

static u8 make_const(Value val) {
    i32 constant = add_constant(current_chunk(),val);

    if (constant > UINT8_MAX) {
        error("TOO MANY CONSTANTS IN A CHUNK OF CODE !!");
        return 0;
    }

    return (u8) constant;
}


static void grouping() {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void binary() {
    TokenType op = parser.prev.type;
    ParseRule* rule = getRule(op);

    if (TOKEN_STAR_STAR == op) {
        parsePrecedence((Precedence) rule->precedence);

    } 
    else {
        parsePrecedence((Precedence) rule->precedence + 1);
    }



    switch (op) {
        case TOKEN_PLUS:                    emit_byte(OP_ADD); break;
        case TOKEN_MINUS:                   emit_byte(OP_SUB); break;
        case TOKEN_STAR:                    emit_byte(OP_MUL); break;
        case TOKEN_SLASH:                   emit_byte(OP_DIV); break;
        case TOKEN_STAR_STAR:               emit_byte(OP_POW); break;
        case TOKEN_BITWISE_AND:             emit_byte(OP_BITWISE_AND); break;
        case TOKEN_BITWISE_OR:              emit_byte(OP_BITWISE_OR); break;
        case TOKEN_BITWISE_XOR:             emit_byte(OP_BITWISE_XOR); break;
        case TOKEN_BITWISE_LEFT_SHIFT:      emit_byte(OP_BITWISE_LEFTSHIFT); break;
        case TOKEN_BITWISE_RIGHT_SHIFT:     emit_byte(OP_BITWISE_RIGHTSHIFT); break;
        case TOKEN_PERCENT:                 emit_byte(OP_MOD); break;

        case TOKEN_BANG_EQUAL:    emit_bytes(OP_EQU, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:   emit_byte(OP_EQU); break;
        case TOKEN_GREATER:       emit_byte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emit_bytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:          emit_byte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    emit_bytes(OP_GREATER, OP_NOT); break;

        default: return;
    }

}

static void unary() {

    TokenType operatorType = parser.prev.type;


    parsePrecedence(PREC_POWER);  // compile the operand

    switch (operatorType) {
        case TOKEN_MINUS:       emit_byte(OP_NEGATE); break;
        case TOKEN_BITWISE_NOT: emit_byte(OP_BITWISE_NOT); break;
        case TOKEN_BANG:        emit_byte(OP_NOT); break;
        default: return;
    }
}



static void emit_const(Value value) {
    emit_bytes(OP_CONST,make_const(value));
}

static void number() {
    double val = strtod(parser.prev.start,NULL);

    emit_const(NUMBER_VAL(val));
}

static void emit_byte(u8 byte) {
    write_chunk(current_chunk(), byte, parser.prev.line);
}

static void emit_bytes(u8 byte1, u8 byte2) {
  emit_byte(byte1);
  emit_byte(byte2);
}


static Chunk* current_chunk() {
    return compiling_chunk;
}

static void end_compiler() {


    emit_return();

#ifdef LUNAR_DEBUG_PRINT_CODE
    if (!parser.had_err) {
        disassemble_chunk(current_chunk(), "code");
    }
#endif

}

static void emit_return() {
    emit_byte(OP_RET);
}

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_BITWISE_NOT]   = {unary,    NULL, PREC_UNARY},
  [TOKEN_BITWISE_OR]    = {NULL,    binary, PREC_BIT_OR},
  [TOKEN_BITWISE_XOR]    = {NULL,    binary, PREC_BIT_XOR},
  [TOKEN_BITWISE_AND]    = {NULL,    binary, PREC_BIT_AND},
  [TOKEN_BITWISE_LEFT_SHIFT]        = {NULL,    binary, PREC_SHIFT},
  [TOKEN_BITWISE_RIGHT_SHIFT]       = {NULL,    binary, PREC_SHIFT},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_PERCENT]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR_STAR]     = {NULL,     binary, PREC_POWER},
  [TOKEN_BANG]          = {unary,     NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary,   PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary,   PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,     NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,     NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,     NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_STRUCT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RET]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,     NULL,   PREC_NONE},
  [TOKEN_LET]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};


static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void parsePrecedence(Precedence precedence) {
    advance();

    ParseFn prefixRule = getRule(parser.prev.type)->prefix;

    if (NULL == prefixRule) {
        error("Expected Expression");
        return;
    }

    prefixRule();


    while (precedence <= getRule(parser.curr.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.prev.type)->infix;

        if ( NULL == infixRule ) {
            error("Invalid binary expression");
            return;
        }

        infixRule();
    }
}

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

bool compile(const char* src, Chunk* chunk) {
    init_scanner(src);

    compiling_chunk = chunk;
    
    parser.had_err      = false;
    parser.panic_mode   = false;

    advance();
    expression();
    consume(TOKEN_EOF, "Expect end of expression.");
    end_compiler();
    return !parser.had_err;
    
}
