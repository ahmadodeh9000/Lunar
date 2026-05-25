#ifndef lunar_value_h
#define lunar_value_h

#include "common.h"

typedef double Value;

typedef struct {
    i32 count;
    i32 capacity;
    Value* values;
} ValueArray;


void init_value_array(ValueArray* va);
void write_value_array(ValueArray* va, Value v);
void free_value_array(ValueArray* va);
void printValue(Value value);


#endif