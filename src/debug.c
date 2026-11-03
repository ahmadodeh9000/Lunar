#include "debug.h"
#include "object.h"
#include "value.h"
#include <stdio.h>

static i32 simple_instruction(const char* name, i32 offset) {
    printf("%s\n", name);
    return offset + 1;
}

static i32 constant_instruction(const char* name, Chunk* chunk, i32 offset) {
    u8 c = chunk->code[offset + 1];
    printf("%-20s %4d '", name, c);
    printValue(chunk->constants.values[c]);
    printf("'\n");
    return offset + 2;
}

static i32 byte_instruction(const char* name, Chunk* chunk, i32 offset) {
    printf("%-20s %4d\n", name, chunk->code[offset + 1]);
    return offset + 2;
}

static i32 jump_instruction(const char* name, int sign, Chunk* chunk, i32 offset) {
    u16 jump = (u16)((chunk->code[offset+1] << 8) | chunk->code[offset+2]);
    printf("%-20s %4d -> %d\n", name, offset, offset + 3 + sign * (int)jump);
    return offset + 3;
}

static i32 for_iter_instruction(Chunk* chunk, i32 offset) {
    u8  slot  = chunk->code[offset + 1];
    u8  nvars = chunk->code[offset + 2];
    u16 jump  = (u16)((chunk->code[offset+3] << 8) | chunk->code[offset+4]);
    printf("%-20s %4d (%d vars) -> %d\n", "OP_FOR_ITER", slot, nvars, offset + 5 + (int)jump);
    return offset + 5;
}

static i32 invoke_instruction(const char* name, Chunk* chunk, i32 offset) {
    u8 c    = chunk->code[offset + 1];
    u8 argc = chunk->code[offset + 2];
    printf("%-20s %4d '", name, c);
    printValue(chunk->constants.values[c]);
    printf("' (%d args)\n", argc);
    return offset + 3;
}

static i32 closure_instruction(Chunk* chunk, i32 offset) {
    offset++;
    u8 c = chunk->code[offset++];
    printf("%-20s %4d '", "OP_CLOSURE", c);
    printValue(chunk->constants.values[c]);
    printf("'\n");
    ObjFunction* fn = AS_FUNCTION(chunk->constants.values[c]);
    for (int j = 0; j < fn->upvalue_count; j++) {
        int is_local = chunk->code[offset++];
        int index    = chunk->code[offset++];
        printf("%04d    |                         %s %d\n",
               offset - 2, is_local ? "local" : "upvalue", index);
    }
    return offset;
}

static const char* wide_name(u8 op) {
    switch (op) {
        case OP_CONST:         return "OP_CONST";
        case OP_DEFINE_GLOBAL: return "OP_DEFINE_GLOBAL";
        case OP_GET_GLOBAL:    return "OP_GET_GLOBAL";
        case OP_SET_GLOBAL:    return "OP_SET_GLOBAL";
        case OP_CLOSURE:       return "OP_CLOSURE";
        case OP_STRUCT:        return "OP_STRUCT";
        case OP_GET_PROPERTY:  return "OP_GET_PROPERTY";
        case OP_SET_PROPERTY:  return "OP_SET_PROPERTY";
        case OP_METHOD:        return "OP_METHOD";
        case OP_INVOKE:        return "OP_INVOKE";
        case OP_GET_SUPER:     return "OP_GET_SUPER";
        case OP_SUPER_INVOKE:  return "OP_SUPER_INVOKE";
        default:               return NULL;
    }
}

// OP_WIDE <op> <3-byte index> <op's other operands>
static i32 wide_instruction(Chunk* chunk, i32 offset) {
    u8 op = chunk->code[offset + 1];
    const char* name = wide_name(op);
    if (name == NULL) {
        printf("OP_WIDE with unknown opcode %d\n", op);
        return offset + 2;
    }
    i32 c = (chunk->code[offset+2] << 16) | (chunk->code[offset+3] << 8) | chunk->code[offset+4];
    printf("OP_WIDE %-12s %4d '", name, c);
    printValue(chunk->constants.values[c]);
    offset += 5;
    if (op == OP_INVOKE || op == OP_SUPER_INVOKE) {
        printf("' (%d args)\n", chunk->code[offset]);
        return offset + 1;
    }
    printf("'\n");
    if (op == OP_CLOSURE) {
        ObjFunction* fn = AS_FUNCTION(chunk->constants.values[c]);
        for (int j = 0; j < fn->upvalue_count; j++) {
            int is_local = chunk->code[offset++];
            int index    = chunk->code[offset++];
            printf("%04d    |                         %s %d\n",
                   offset - 2, is_local ? "local" : "upvalue", index);
        }
    }
    return offset;
}

void disassemble_chunk(Chunk* chunk, const char* name) {
    printf("\n== %s ==\n", name);
    for (i32 off = 0; off < chunk->count;)
        off = disassemble_instruction(chunk, off);
}

i32 disassemble_instruction(Chunk* chunk, i32 offset) {
    printf("%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset-1])
        printf("   | ");
    else
        printf("%4d ", chunk->lines[offset]);

    u8 instr = chunk->code[offset];
    switch (instr) {
        case OP_CONST:              return constant_instruction("OP_CONST", chunk, offset);
        case OP_NIL:                return simple_instruction("OP_NIL", offset);
        case OP_TRUE:               return simple_instruction("OP_TRUE", offset);
        case OP_FALSE:              return simple_instruction("OP_FALSE", offset);
        case OP_POP:                return simple_instruction("OP_POP", offset);
        case OP_GET_LOCAL:          return byte_instruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:          return byte_instruction("OP_SET_LOCAL", chunk, offset);
        case OP_DEFINE_GLOBAL:      return constant_instruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_GET_GLOBAL:         return constant_instruction("OP_GET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:         return constant_instruction("OP_SET_GLOBAL", chunk, offset);
        case OP_GET_UPVALUE:        return byte_instruction("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE:        return byte_instruction("OP_SET_UPVALUE", chunk, offset);
        case OP_CLOSE_UPVALUE:      return simple_instruction("OP_CLOSE_UPVALUE", offset);
        case OP_NEGATE:             return simple_instruction("OP_NEGATE", offset);
        case OP_ADD:                return simple_instruction("OP_ADD", offset);
        case OP_SUB:                return simple_instruction("OP_SUB", offset);
        case OP_MUL:                return simple_instruction("OP_MUL", offset);
        case OP_DIV:                return simple_instruction("OP_DIV", offset);
        case OP_MOD:                return simple_instruction("OP_MOD", offset);
        case OP_POW:                return simple_instruction("OP_POW", offset);
        case OP_BITWISE_AND:        return simple_instruction("OP_BITWISE_AND", offset);
        case OP_BITWISE_OR:         return simple_instruction("OP_BITWISE_OR", offset);
        case OP_BITWISE_XOR:        return simple_instruction("OP_BITWISE_XOR", offset);
        case OP_BITWISE_NOT:        return simple_instruction("OP_BITWISE_NOT", offset);
        case OP_BITWISE_LEFTSHIFT:  return simple_instruction("OP_BITWISE_LEFTSHIFT", offset);
        case OP_BITWISE_RIGHTSHIFT: return simple_instruction("OP_BITWISE_RIGHTSHIFT", offset);
        case OP_NOT:                return simple_instruction("OP_NOT", offset);
        case OP_EQU:                return simple_instruction("OP_EQU", offset);
        case OP_GREATER:            return simple_instruction("OP_GREATER", offset);
        case OP_LESS:               return simple_instruction("OP_LESS", offset);
        case OP_PRINT:              return simple_instruction("OP_PRINT", offset);
        case OP_JUMP:               return jump_instruction("OP_JUMP", +1, chunk, offset);
        case OP_JUMP_IF_FALSE:      return jump_instruction("OP_JUMP_IF_FALSE", +1, chunk, offset);
        case OP_LOOP:               return jump_instruction("OP_LOOP", -1, chunk, offset);
        case OP_CALL:               return byte_instruction("OP_CALL", chunk, offset);
        case OP_CLOSURE:            return closure_instruction(chunk, offset);
        case OP_RET:                return simple_instruction("OP_RET", offset);
        case OP_STRUCT:             return constant_instruction("OP_STRUCT", chunk, offset);
        case OP_GET_PROPERTY:       return constant_instruction("OP_GET_PROPERTY", chunk, offset);
        case OP_SET_PROPERTY:       return constant_instruction("OP_SET_PROPERTY", chunk, offset);
        case OP_METHOD:             return constant_instruction("OP_METHOD", chunk, offset);
        case OP_INVOKE:             return invoke_instruction("OP_INVOKE", chunk, offset);
        case OP_INHERIT:            return simple_instruction("OP_INHERIT", offset);
        case OP_GET_SUPER:          return constant_instruction("OP_GET_SUPER", chunk, offset);
        case OP_SUPER_INVOKE:       return invoke_instruction("OP_SUPER_INVOKE", chunk, offset);
        case OP_BUILD_ARRAY:        return byte_instruction("OP_BUILD_ARRAY", chunk, offset);
        case OP_GET_INDEX:          return simple_instruction("OP_GET_INDEX", offset);
        case OP_SET_INDEX:          return simple_instruction("OP_SET_INDEX", offset);
        case OP_APPEND_ARRAY:       return byte_instruction("OP_APPEND_ARRAY", chunk, offset);
        case OP_BUILD_MAP:          return byte_instruction("OP_BUILD_MAP", chunk, offset);
        case OP_APPEND_MAP:         return byte_instruction("OP_APPEND_MAP", chunk, offset);
        case OP_ITER_PREP:          return byte_instruction("OP_ITER_PREP", chunk, offset);
        case OP_FOR_ITER:           return for_iter_instruction(chunk, offset);
        case OP_TRY:                return jump_instruction("OP_TRY", +1, chunk, offset);
        case OP_POP_HANDLER:        return simple_instruction("OP_POP_HANDLER", offset);
        case OP_THROW:              return simple_instruction("OP_THROW", offset);
        case OP_WIDE:               return wide_instruction(chunk, offset);
        default:
            printf("Unknown opcode %d\n", instr);
            return offset + 1;
    }
}
