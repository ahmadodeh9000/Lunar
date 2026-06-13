#ifndef lunar_object_h
#define lunar_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_STRUCT,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
} ObjType;

struct Obj {
    ObjType type;
    bool is_marked;         // GC mark bit
    struct Obj* next;
};

/* ── String ── */
struct ObjString {
    Obj obj;
    i32 length;
    u32 hash;
    char* chars;
};

/* ── Upvalue (captured variable) ── */
typedef struct ObjUpvalue {
    Obj obj;
    Value* location;        // points into stack while open
    Value  closed;          // holds value after close
    struct ObjUpvalue* next;
} ObjUpvalue;

/* ── Function ── */
typedef struct {
    Obj obj;
    int arity;
    int upvalue_count;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

/* ── Native function ── */
typedef Value (*NativeFn)(int argc, Value* args);

typedef struct {
    Obj obj;
    NativeFn function;
    const char* name;
} ObjNative;

/* ── Closure ── */
typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalue_count;
} ObjClosure;

/* ── Struct (class) definition ── */
typedef struct {
    Obj obj;
    ObjString* name;
    Table* methods;
} ObjStruct;

/* ── Instance ── */
typedef struct {
    Obj obj;
    ObjStruct* klass;
    Table* fields;
} ObjInstance;

/* ── Bound method ── */
typedef struct {
    Obj obj;
    Value receiver;
    ObjClosure* method;
} ObjBoundMethod;


/* ──────── helpers ──────── */

static inline bool is_obj_type(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#define OBJ_TYPE(value)         (AS_OBJ(value)->type)

#define IS_STRING(value)        is_obj_type(value, OBJ_STRING)
#define IS_FUNCTION(value)      is_obj_type(value, OBJ_FUNCTION)
#define IS_NATIVE(value)        is_obj_type(value, OBJ_NATIVE)
#define IS_CLOSURE(value)       is_obj_type(value, OBJ_CLOSURE)
#define IS_STRUCT(value)        is_obj_type(value, OBJ_STRUCT)
#define IS_INSTANCE(value)      is_obj_type(value, OBJ_INSTANCE)
#define IS_BOUND_METHOD(value)  is_obj_type(value, OBJ_BOUND_METHOD)

#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value)      ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)        (((ObjNative*)AS_OBJ(value))->function)
#define AS_CLOSURE(value)       ((ObjClosure*)AS_OBJ(value))
#define AS_STRUCT(value)        ((ObjStruct*)AS_OBJ(value))
#define AS_INSTANCE(value)      ((ObjInstance*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)  ((ObjBoundMethod*)AS_OBJ(value))


ObjString*      copy_str(const char* chars, int length);
ObjString*      take_str(char* chars, int length);
ObjFunction*    new_function();
ObjNative*      new_native(NativeFn fn, const char* name);
ObjClosure*     new_closure(ObjFunction* function);
ObjUpvalue*     new_upvalue(Value* slot);
ObjStruct*      new_struct(ObjString* name);
ObjInstance*    new_instance(ObjStruct* klass);
ObjBoundMethod* new_bound_method(Value receiver, ObjClosure* method);

void printObject(Value value);

#endif
