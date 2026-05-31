#ifndef lunar_vm
#define lunar_vm

#include "chunk.h"
#include "value.h"

#define STACK_MAX   256

typedef struct {
    Chunk* chunk;
    u8* ip;
    Value stack[STACK_MAX];
    Value* stack_top;
    Obj* objects;
} LunarVM;


typedef enum {
    INTERPRET_OK,
    INTERPRET_RUNTIME_ERR,
    INTERPRET_COMPPILE_ERR,
} InterpretResult; 

extern LunarVM lvm;

void init_lunar_vm();
void free_lunar_vm();

InterpretResult interpret(const char* src);
void push(Value val);
Value pop();

#endif