#ifndef lunar_chunk_h
#define lunar_chunk_h

#include "common.h"
#include "value.h"

// Every opcode, in order. The enum below and run()'s dispatch table are both made
// from this list, so an opcode can't be left out of either: with computed goto, an
// opcode that has no handler in run() is a compile error.
#define LUNAR_OPCODES(X) \
    X(OP_CONST) \
    X(OP_NIL) X(OP_TRUE) X(OP_FALSE) \
    X(OP_POP) \
    X(OP_GET_LOCAL) X(OP_SET_LOCAL) \
    X(OP_DEFINE_GLOBAL) X(OP_GET_GLOBAL) X(OP_SET_GLOBAL) \
    X(OP_GET_UPVALUE) X(OP_SET_UPVALUE) X(OP_CLOSE_UPVALUE) \
    X(OP_NEGATE) \
    X(OP_ADD) X(OP_SUB) X(OP_MUL) X(OP_DIV) X(OP_MOD) X(OP_POW) \
    X(OP_BITWISE_AND) X(OP_BITWISE_OR) X(OP_BITWISE_XOR) \
    X(OP_BITWISE_NOT) X(OP_BITWISE_LEFTSHIFT) X(OP_BITWISE_RIGHTSHIFT) \
    X(OP_NOT) \
    X(OP_EQU) X(OP_GREATER) X(OP_LESS) \
    X(OP_PRINT) \
    X(OP_JUMP) X(OP_JUMP_IF_FALSE) X(OP_LOOP) \
    X(OP_CALL) X(OP_CLOSURE) X(OP_RET) \
    X(OP_STRUCT) \
    X(OP_GET_PROPERTY) X(OP_SET_PROPERTY) \
    X(OP_METHOD) \
    X(OP_INVOKE) \
    X(OP_INHERIT) \
    X(OP_GET_SUPER) X(OP_SUPER_INVOKE) \
    X(OP_BUILD_ARRAY) X(OP_GET_INDEX) X(OP_SET_INDEX) \
    X(OP_APPEND_ARRAY) \
    X(OP_BUILD_MAP) X(OP_APPEND_MAP) \
    X(OP_ITER_PREP) X(OP_FOR_ITER) \
    X(OP_TRY) X(OP_POP_HANDLER) X(OP_THROW) \
    X(OP_BUILD_STRING) \
    X(OP_IMPORT) X(OP_MODULE_RET) \
    X(OP_WIDE)      /* prefix: the next opcode's constant index is 3 bytes, not 1 */

typedef enum {
#define LUNAR_OPCODE_ENUM(op) op,
    LUNAR_OPCODES(LUNAR_OPCODE_ENUM)
#undef LUNAR_OPCODE_ENUM
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
