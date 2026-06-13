#ifndef lunar_value_h
#define lunar_value_h

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum { VAL_BOOL, VAL_NIL, VAL_OBJ, VAL_NUM } ValueType;

typedef struct {
    ValueType type;
    union { bool boolean; double number; Obj* obj; } as;
} Value;

typedef struct {
    i32    count;
    i32    capacity;
    Value* values;
} ValueArray;

#define IS_BOOL(v)    ((v).type == VAL_BOOL)
#define IS_NIL(v)     ((v).type == VAL_NIL)
#define IS_NUMBER(v)  ((v).type == VAL_NUM)
#define IS_OBJ(v)     ((v).type == VAL_OBJ)

#define AS_BOOL(v)    ((v).as.boolean)
#define AS_NUMBER(v)  ((v).as.number)
#define AS_OBJ(v)     ((v).as.obj)

#define BOOL_VAL(v)   ((Value){VAL_BOOL, {.boolean = (v)}})
#define NIL_VAL       ((Value){VAL_NIL,  {.number  = 0}})
#define NUMBER_VAL(v) ((Value){VAL_NUM,  {.number  = (v)}})
#define OBJ_VAL(v)    ((Value){VAL_OBJ,  {.obj     = (Obj*)(v)}})

void init_value_array(ValueArray* va);
void write_value_array(ValueArray* va, Value v);
void free_value_array(ValueArray* va);
void printValue(Value value);
bool values_equ(Value a, Value b);

#endif
