#include "value.h"
#include "object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_value_array(ValueArray* va) {
    va->capacity = 0; va->count = 0; va->values = NULL;
}

void write_value_array(ValueArray* va, Value v) {
    if (va->capacity < va->count + 1) {
        i32 old = va->capacity;
        va->capacity = old < 8 ? 8 : 2 * old;
        va->values = (Value*)realloc(va->values, sizeof(Value) * va->capacity);
        if (!va->values) { fprintf(stderr,"OOM\n"); exit(1); }
    }
    va->values[va->count++] = v;
}

void free_value_array(ValueArray* va) {
    free(va->values);
    init_value_array(va);
}

void printValue(Value value) {
    switch (value.type) {
        case VAL_BOOL: printf(AS_BOOL(value) ? "true" : "false"); break;
        case VAL_NIL:  printf("nil"); break;
        case VAL_NUM:  printf("%g", AS_NUMBER(value)); break;
        case VAL_OBJ:  printObject(value); break;
    }
}

bool values_equ(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:  return true;
        case VAL_NUM:  return AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_OBJ:  return AS_OBJ(a) == AS_OBJ(b); // interned strings → ptr eq
        default:       return false;
    }
}
