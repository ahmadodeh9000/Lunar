#ifndef lunar_chunk_h
#define lunar_chunk_h

#include "common.h"
#include "value.h"


typedef enum {

    // control flow
    OP_RET,             
    OP_CONST,         
    
    // true, false, nil
    OP_TRUE,OP_FALSE,OP_NIL,

    // arithmetic
    OP_NEGATE, OP_ADD, OP_SUB, OP_MUL, OP_DIV,
    OP_POW, OP_MOD, OP_NOT, OP_EQU, OP_GREATER,
    OP_LESS,

    // bitwise
    OP_BITWISE_OR, OP_BITWISE_XOR, OP_BITWISE_AND,
    OP_BITWISE_NOT, OP_BITWISE_LEFTSHIFT,
    OP_BITWISE_RIGHTSHIFT,

} OpCode;


typedef struct {
    i32 count;
    i32 capacity;
    u8 *code;
    ValueArray constants;
    i32* lines;
} Chunk;


void init_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, u8 byte, i32 line);
i32 add_constant(Chunk* chunk, Value val);  /* we return the index where the constant was appended so that we can locate that same constant later*/
void free_chunk(Chunk* chunk);

#endif