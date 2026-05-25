
#include "debug.h"
#include "value.h"

#include <stdio.h>



/* HELPER FUNCTIONS */
static i32 simple_instruction(const char* name, i32 offset);
static i32 constant_instruction(const char* name, Chunk* chunk, i32 offset);



void disassemble_chunk(Chunk* chunk, const char* name) {
    printf("\n\n======== %s %s ========\n\n",LUNAR_NAME,LUNAR_VERSION);
    printf("=== %s ===\n", name);

    for (i32 offset = 0; offset < chunk->count;) {
        offset = disassemble_instruction(chunk,offset);
    }
}

/* disassembling instructions to something more readable */

i32 disassemble_instruction(Chunk* chunk, i32 offset) {
    
    printf("%04d ", offset);

    if (
        offset > 0 && 
        chunk->lines[offset] == chunk->lines[offset - 1]
    ) {
        printf("   |");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    u8 instr = chunk->code[offset];

    switch (instr) {
        case OP_RET:
            return simple_instruction("OP_RET",offset);

        case OP_CONST:
            return constant_instruction("OP_CONST",chunk,offset);
        
        case OP_NEGATE:
            return simple_instruction("OP_NEGATE",offset);

        case OP_ADD:
            return simple_instruction("OP_ADD",offset);

        case OP_SUB:
            return simple_instruction("OP_SUB",offset);

        case OP_MUL:
            return simple_instruction("OP_MUL",offset);

        case OP_DIV:
            return simple_instruction("OP_DIV",offset);

        case OP_MOD:
            return simple_instruction("OP_MOD",offset);

        case OP_POW:
            return simple_instruction("OP_POW",offset);

        case OP_BITWISE_AND:
            return simple_instruction("OP_BITWISE_AND",offset);

        case OP_BITWISE_OR:
            return simple_instruction("OP_BITWISE_OR",offset);

        case OP_BITWISE_XOR:
            return simple_instruction("OP_BITWISE_XOR",offset);

        case OP_BITWISE_NOT:
            return simple_instruction("OP_BITWISE_NOT",offset);

        case OP_BITWISE_RIGHTSHIFT:
            return simple_instruction("OP_BITWISE_RIGHTSHIFT",offset);

        case OP_BITWISE_LEFTSHIFT:
            return simple_instruction("OP_BITWISE_LEFTSHIFT",offset);
        

        default:
            printf("Unknown instruction !\n");
            return offset + 1;
    }
}


static i32 simple_instruction(const char* name, i32 offset) {
  printf("%s\n", name);
  return offset + 1;
}

static i32 constant_instruction(const char* name, Chunk* chunk, i32 offset) {
    u8 constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");

    return offset + 2;  // OPCODE , NUM (2) thats why offset + 2

}