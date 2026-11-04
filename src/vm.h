#ifndef lunar_vm_h
#define lunar_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

#define STACK_MAX       (FRAMES_MAX * 256)
#define FRAMES_MAX      1024
#define HANDLERS_MAX    (FRAMES_MAX * 2)

typedef struct {
    ObjClosure* closure;
    u8* ip;
    Value* slots;       // pointer into vm stack — base of this frame's locals
} CallFrame;

// a running try block: where its catch starts, and the VM state to go back to
typedef struct {
    int    frame_count;
    Value* stack_top;
    u8*    catch_ip;
} Handler;

typedef struct {
    // the fields run() touches on every instruction come first, before the big
    // arrays, so they're at small offsets from lvm
    Value*  stack_top;
    int frame_count;

    CallFrame frames[FRAMES_MAX];
    Value   stack[STACK_MAX];

    Table   builtins;   // natives, copied into every module's globals
    Table   modules;    // canonical path -> ObjModule, the import cache
    Table   builtin_modules;    // name -> ObjMap, for import("string") and the like
    struct ObjModule* main_module;   // the script being run, or the REPL
    char*   main_dir;   // its directory; module names are shown relative to it
    Table   strings;    // interned strings

    ObjString* init_string;     // cached "init" identifier for constructors

    ObjUpvalue* open_upvalues;  // linked list of open upvalues

    // modules whose top-level code is running, outermost first, for finding cycles
    struct { struct ObjModule* module; int frame; } loading[FRAMES_MAX];
    int     loading_count;

    Handler handlers[HANDLERS_MAX];
    int     handler_count;
    bool    error_pending;      // set by runtime_error until the error is caught or reported
    Value   error_value;        // the error being thrown (a GC root)

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

// path is the script's file, or NULL for the REPL
InterpretResult interpret(const char* src, const char* path);
void push(Value val);
Value pop();

// Raises a catchable error with this message. It doesn't jump anywhere: the caller
// must return right away (natives return any value; the VM sees error_pending).
void runtime_error(const char* fmt, ...);

void define_native(const char* name, NativeFn fn);
// adds a native to a built-in module's map, which the caller keeps reachable
void define_module_native(ObjMap* map, const char* name, NativeFn fn);

// bytes in the UTF-8 character at s; a malformed one counts as a single byte
int utf8_char_length(const char* s, int remaining);

#endif
