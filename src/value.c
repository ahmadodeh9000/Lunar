#include "value.h"

#include <stdio.h>
#include <stdlib.h>

void init_value_array(ValueArray* va) {

    /* checks if value array is null or not*/
    if (NULL == va) {
        fprintf(stderr,"VALUEARRAY IS NULL !\n");
        exit(-67);
    }

    va->capacity    = 0;
    va->count       = 0;
    va->values      = NULL;

}

void write_value_array(ValueArray* va, Value v) {

    /* checks if value array is null or not*/
    if (NULL == va) {
        fprintf(stderr,"VALUEARRAY IS NULL !\n");
        exit(-67);
    }

    /* reallocate value array logic */
    if (va->capacity < va->count + 1) {
        i32 old_capacity = va->capacity;
        va->capacity = ((old_capacity < 8) ? 8 : 2 * old_capacity);
        va->values  = (Value*) realloc(va->values, sizeof(Value) * va->capacity);

        if (NULL == va->values) {
            fprintf(stderr,"FAILED TO REALLOCATE VALUEARRAY VALUES !!\n");
            exit(-67);
        }


    }

    va->values[va->count++] = v;

}

void free_value_array(ValueArray* va) {

    /* checks if value array is null or not*/
    if (NULL == va) {
        fprintf(stderr,"VALUEARRAY IS NULL !\n");
        exit(-67);
    }

    free(va->values);
    init_value_array(va);
}

void printValue(Value value) {
    
    switch(value.type) {

        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        
        case VAL_NIL:
            printf("nil");
            break;

        case VAL_NUM:
            printf("%g",AS_NUMBER(value));
            break;


    }

}

bool values_equ(Value a, Value b) {

    if (a.type != b.type) return false;


    switch(a.type) {
        case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL: return true;
        case VAL_NUM: return AS_NUMBER(a) == AS_NUMBER(b);

        default: return false;
    }


}