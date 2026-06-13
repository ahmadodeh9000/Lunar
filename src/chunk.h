#ifndef lunar_chunk_h
#define lunar_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
    OP_CONST,
    OP_NIL, OP_TRUE, OP_FALSE,
    OP_POP,
    OP_GET_LOCAL, OP_SET_LOCAL,
    OP_DEFINE_GLOBAL, OP_GET_GLOBAL, OP_SET_GLOBAL,
    OP_GET_UPVALUE, OP_SET_UPVALUE, OP_CLOSE_UPVALUE,
    OP_NEGATE,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    OP_BITWISE_AND, OP_BITWISE_OR, OP_BITWISE_XOR,
    OP_BITWISE_NOT, OP_BITWISE_LEFTSHIFT, OP_BITWISE_RIGHTSHIFT,
    OP_NOT,
    OP_EQU, OP_GREATER, OP_LESS,
    OP_PRINT,
    OP_JUMP, OP_JUMP_IF_FALSE, OP_LOOP,
    OP_CALL, OP_CLOSURE, OP_RET,
    OP_STRUCT,
    OP_GET_PROPERTY, OP_SET_PROPERTY,
    OP_METHOD,
    OP_INVOKE,
    OP_INHERIT,
    OP_GET_SUPER, OP_SUPER_INVOKE,
} OpCode;

typedef struct {
    i32  count;
    i32  capacity;
    u8*  code;
    i32* lines;
    ValueArray constants;
} Chunk;

void init_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, u8 byte, i32 line);
i32  add_constant(Chunk* chunk, Value val);
void free_chunk(Chunk* chunk);

#endif
