#ifndef lunar_object_h
#define lunar_object_h

#include "common.h"
#include "value.h"



typedef enum {
    OBJ_STRING,

} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;
};

struct ObjString {
    Obj Obj;
    i32 length;
    char* chars;
};

static inline bool is_obj_type(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}


#define OBJ_TYPE(value)        (AS_OBJ(value)->type)
#define IS_STRING(value)       is_obj_type(value,OBJ_STRING) 

#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)



ObjString* copy_str(const char* chars, int length);
void printObject(Value value);
ObjString* take_str(char* chars, int length);


#endif