#ifndef lunar_value_h
#define lunar_value_h

#include "common.h"

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUM,
} ValueType;


typedef struct  {
    ValueType type;

    union {
        bool boolean;
        double number;
    } as;
} Value;


typedef struct {
    i32 count;
    i32 capacity;
    Value* values;
} ValueArray;



// value helpers


#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUM)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)

#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUM, {.number = value}})

void init_value_array(ValueArray* va);
void write_value_array(ValueArray* va, Value v);
void free_value_array(ValueArray* va);
void printValue(Value value);

bool values_equ(Value a, Value b);

#endif