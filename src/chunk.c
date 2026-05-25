
#include "chunk.h"
#include <stdlib.h>
#include <stdio.h>


void init_chunk(Chunk* chunk) {
    

    /* check if chunk is null (used yoda case lol)*/
    if (NULL == chunk) {
        fprintf(stderr,"CHUNK IS NULL\n");
        exit(-67);
    }


    /* default values for chunk */

    chunk->count    = 0;
    chunk->capacity = 0;
    chunk->code     = NULL;
    chunk->lines    = NULL;

    init_value_array(&chunk->constants);

}

void write_chunk(Chunk* chunk, u8 byte, i32 line) {

    /* check if chunk is null */
    if (NULL == chunk) {
        fprintf(stderr,"CHUNK IS NULL\n");
        exit(-67);
    }

    if (chunk->capacity < chunk->count + 1) {
        i32 old_capacity    = chunk->capacity;                          
        chunk->capacity     = (old_capacity < 8 ? 8 : 2 * old_capacity); 
        
        chunk->code = (u8*) realloc(chunk->code, sizeof(u8) * chunk->capacity);
        chunk->lines= (i32*)realloc(chunk->lines, sizeof(i32) * chunk->capacity);

        if (NULL == chunk->code || NULL == chunk->lines) {
            fprintf(stderr, "FAILED TO REALLOCATE CHUNK CODE !!!\n");
            exit(-67);
        }

    }

    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count++] = line;
    
}

i32 add_constant(Chunk* chunk, Value val) {
    write_value_array(&chunk->constants,val);
    return chunk->constants.count - 1;
}


void free_chunk(Chunk* chunk) {
    
    /* check if chunk is null */
    if (NULL == chunk) {
        fprintf(stderr,"CHUNK IS NULL\n");
        exit(-67);
    }

    free(chunk->code);
    free(chunk->lines);
    free_value_array(&chunk->constants);
    init_chunk(chunk);

}

