#ifndef lunar_vm_h
#define lunar_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

#define STACK_MAX       (FRAMES_MAX * 256)
#define FRAMES_MAX      64

typedef struct {
    ObjClosure* closure;
    u8* ip;
    Value* slots;       // pointer into vm stack — base of this frame's locals
} CallFrame;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frame_count;

    Value   stack[STACK_MAX];
    Value*  stack_top;

    Table   globals;    // global variables
    Table   strings;    // interned strings

    ObjString* init_string;     // cached "init" identifier for constructors

    ObjUpvalue* open_upvalues;  // linked list of open upvalues

    // GC state
    size_t bytes_allocated;
    size_t next_gc;
    Obj*   objects;             // linked list of all heap objects
    int    gray_count;
    int    gray_capacity;
    Obj**  gray_stack;
} LunarVM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_RUNTIME_ERR,
    INTERPRET_COMPILE_ERR,
} InterpretResult;

extern LunarVM lvm;

void init_lunar_vm();
void free_lunar_vm();

InterpretResult interpret(const char* src);
void push(Value val);
Value pop();

#endif
