#ifndef lunar_debug_h
#define lunar_debug_h

#include "common.h"
#include "chunk.h"

void disassemble_chunk(Chunk* chunk, const char* name);
i32 disassemble_instruction(Chunk* chunk, i32 offset);


#endif

