#ifndef lunar_scanner_h
#define lunar_scanner_h

#include "common.h"

typedef enum {
    // Single-character tokens.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_MINUS, TOKEN_PLUS, TOKEN_DOT,
    TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,
    TOKEN_BITWISE_NOT, TOKEN_BITWISE_OR,
    TOKEN_BITWISE_XOR, TOKEN_BITWISE_AND,
    TOKEN_PERCENT,

    // One or two character tokens.
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_AND, TOKEN_OR, TOKEN_STAR_STAR,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    TOKEN_BITWISE_LEFT_SHIFT,
    TOKEN_BITWISE_RIGHT_SHIFT, TOKEN_COMMA,

    // Literals.
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,

    // Keywords.
    TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_FN, TOKEN_IF, TOKEN_NIL,
    TOKEN_PRINT, TOKEN_RET,
    TOKEN_TRUE, TOKEN_LET, TOKEN_WHILE, TOKEN_STRUCT,
    TOKEN_SELF, TOKEN_SUPER,

    TOKEN_ERROR, TOKEN_EOF
} TokenType;

typedef struct {
    TokenType   type;
    i32         length;
    i32         line;
    const char* start;
} Token;

void  init_scanner(const char* src);
Token scan_token();

#endif
