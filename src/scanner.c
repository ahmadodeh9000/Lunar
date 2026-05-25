#include "scanner.h"

#include "common.h"
#include <string.h>

typedef struct {
    const char* start;
    const char* curr;
    i32 line;
} Scanner;

Scanner scanner;


void init_scanner(const char* src) {
    scanner.start = src;
    scanner.curr  = src;
    scanner.line  = 1;
}

// Helper functions
static bool is_at_end();
static Token make_token(TokenType type);
static Token err_token(const char* err_msg);
static char advance();
static bool match(char expected);
static char peek();
static char peek_next();
static void skip_whitespace();
static Token string();
static Token number();
static bool is_digit(char c);
static Token identifier();
static bool is_alpha(char c);
static TokenType identifier_type();
static TokenType checkKeyword(int start, int length,
    const char* rest, TokenType type);


static TokenType checkKeyword(int start, int length,
    const char* rest, TokenType type) {
    if (scanner.curr - scanner.start == start + length &&
            memcmp(scanner.start + start, rest, length) == 0) {
        
                return type;
    }

  return TOKEN_IDENTIFIER;
}



static TokenType identifier_type() {
    switch(scanner.start[0]) {
        case 'i': return checkKeyword(1,1,"f",TOKEN_IF);
        case 'e': return checkKeyword(1,3,"lse",TOKEN_ELSE);
        case 'l': return checkKeyword(1,2,"et",TOKEN_LET);
        case 'w': return checkKeyword(1,4,"hile",TOKEN_WHILE);
        case 'p': return checkKeyword(1,4,"rint",TOKEN_PRINT);
        case 'r': return checkKeyword(1,2,"et",TOKEN_RET);
        case 'n': return checkKeyword(1,2,"il",TOKEN_NIL);
        case 't': return checkKeyword(1,3,"rue",TOKEN_TRUE);

        case 'f': {
            if (scanner.curr - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'n': return checkKeyword(2,0,"",TOKEN_FN);
                    case 'a': return checkKeyword(2,3,"lse",TOKEN_FALSE);
                    case 'o': return checkKeyword(2,1,"r",TOKEN_FOR);
                }
            }
            break;
        }

    }

    return TOKEN_IDENTIFIER;
}


static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_alpha(char c) {
    return  (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            c == '_';
}

static char advance() {
  scanner.curr++;
  return scanner.curr[-1];
}

static Token identifier() {
    while (is_alpha(peek()) || is_digit(peek())) advance();

    return make_token(identifier_type());
}



static Token number() {
    while (is_digit(peek())) advance();

    if (peek() == '.' && is_digit(peek_next())) {
        advance();

        while (is_digit(peek())) advance();
    }

    return make_token(TOKEN_NUMBER);
}

// lunar supports multi-line strings 
static Token string() {


    while (peek() != '"' && !is_at_end()) {

        if (peek() == '\n') scanner.line ++;
        advance();
    }

    if (is_at_end()) return err_token("Unterminated string.");

    advance(); // closing quot
    return make_token(TOKEN_STRING);
}

static char peek_next() {
  if (is_at_end()) return '\0';
  return scanner.curr[1];
}

static char peek() {
    return *scanner.curr;
}

static void skip_whitespace() {

    for (;;) {
        char c = peek();


        switch(c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;

            case '\n':
                scanner.line++;
                advance();
                break;

            case '/':
                if (peek_next() == '/') {
                    while (peek() != '\n' && !is_at_end()) advance();
                } else {
                    return;
                }
                break;

            default:
                return;
        }
        
    }

}

static bool match(char expected) {
    if (is_at_end()) return false;

    if (*scanner.curr != expected) return false;
    scanner.curr++;
    return true;
}


static bool is_at_end() {
    return *scanner.curr == '\0';
}

static Token make_token(TokenType type) {
    Token tok;
    tok.type    = type;
    tok.start   = scanner.start;
    tok.length  = (i32) (scanner.curr - scanner.start);
    tok.line    = scanner.line;

    return tok;
}

static Token err_token(const char* err_msg) {
    Token err_tok;

    err_tok.type    = TOKEN_ERROR;
    err_tok.start   = err_msg;
    err_tok.length  = (i32) strlen(err_msg);
    err_tok.line    = scanner.line;

    return err_tok;
}


Token scan_token() {

    skip_whitespace();

    scanner.start = scanner.curr;

    if (is_at_end()) return make_token(TOKEN_EOF);

    char c = advance();

    if (is_alpha(c)) return identifier();
    if (is_digit(c)) return number();

    switch (c) {
        case '{': return make_token(TOKEN_LEFT_BRACE);
        case '}': return make_token(TOKEN_RIGHT_BRACE);
        case '(': return make_token(TOKEN_LEFT_PAREN);
        case ')': return make_token(TOKEN_RIGHT_PAREN);
        case ';': return make_token(TOKEN_SEMICOLON);
        case '+': return make_token(TOKEN_PLUS);
        case '^': return make_token(TOKEN_BITWISE_XOR);
        case '~': return make_token(TOKEN_BITWISE_NOT);
        case '.': return make_token(TOKEN_DOT);
        case '-': return make_token(TOKEN_MINUS);
        case '/': return make_token(TOKEN_SLASH);
        case '"': return string();

        case '!': {
            return make_token(
                match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG
            );
        }

        case '*': {
            return make_token(
                match('*') ? TOKEN_STAR_STAR : TOKEN_STAR
            );
        }

        case '&': {
            return make_token(
                match('&') ? TOKEN_AND : TOKEN_BITWISE_AND
            );
        }

        case '|': {
            return make_token(
                match('|') ? TOKEN_OR : TOKEN_BITWISE_OR
            );
        }


        case '=': {
            return make_token(
                match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL
            );
        }

        case '<': {
            if (match('='))         return make_token(TOKEN_LESS_EQUAL);
            else if (match('<'))    return make_token(TOKEN_BITWISE_LEFT_SHIFT);
            else                    return make_token(TOKEN_LESS);
        }

        case '>': {
            if (match('='))         return make_token(TOKEN_GREATER_EQUAL);
            else if (match('>'))    return make_token(TOKEN_BITWISE_RIGHT_SHIFT);
            else                    return make_token(TOKEN_GREATER);
        }


    }

    return err_token("UNEXPECTED CHARACTER !");

}



