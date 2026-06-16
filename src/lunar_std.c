/*
* I don't if i should keep it or not, since i added FFI ig they are useless for now,
* but anyway no one give a shit so yeah...
*/



#include "lunar_std.h"
#include "vm.h"
#include "value.h"
#include "object.h"

#include <time.h> // for CLOCKS_PER_SEC, don't remove it
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

/* ── natives -- */
static Value clock_native(int argc, Value* args) { return NUMBER_VAL((double)clock()/CLOCKS_PER_SEC); }
static Value sqrt_native(int argc, Value* args)  { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(sqrt(AS_NUMBER(args[0]))); }
static Value abs_native(int argc, Value* args)   { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(fabs(AS_NUMBER(args[0]))); }
static Value floor_native(int argc, Value* args) { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(floor(AS_NUMBER(args[0]))); }
static Value ceil_native(int argc, Value* args)  { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(ceil(AS_NUMBER(args[0]))); }
static Value len_native(int argc, Value* args)   { if(argc!=1||!IS_STRING(args[0]))return NIL_VAL; return NUMBER_VAL((double)AS_STRING(args[0])->length); }
static Value hex_native(int argc, Value* args) {
    if (argc != 1 || !IS_STRING(args[0])) {
        return NIL_VAL;
    }

    const char* hex_str = AS_STRING(args[0])->chars;
    char* end_ptr;

    long number = strtol(hex_str, &end_ptr, 16);

    if (end_ptr == hex_str) {
        runtime_error("Invalid hexadecimal string.");
        return NIL_VAL;
    }

    return NUMBER_VAL((double)number);
}
static Value str_native(int argc, Value* args) {
    if(argc!=1) return NIL_VAL;
    if(IS_STRING(args[0])) return args[0];
    char buf[64]; int len;
    if     (IS_NUMBER(args[0])) len=snprintf(buf,sizeof(buf),"%g",AS_NUMBER(args[0]));
    else if(IS_BOOL(args[0]))   len=snprintf(buf,sizeof(buf),"%s",AS_BOOL(args[0])?"true":"false");
    else if(IS_NIL(args[0]))    len=snprintf(buf,sizeof(buf),"nil");
    else return NIL_VAL;
    return OBJ_VAL(copy_str(buf,len));
}



void register_std_natives() {

    define_native("clock",clock_native);
    define_native("sqrt",sqrt_native);
    define_native("abs",abs_native);
    define_native("floor",floor_native);
    define_native("ceil",ceil_native);
    define_native("str",str_native);
    define_native("len",len_native);
    define_native("hex",hex_native);
}

