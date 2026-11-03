/*
* I don't if i should keep it or not, since i added FFI ig they are useless for now,
* but anyway no one give a shit so yeah...
*/



#include "lunar_std.h"
#include "vm.h"
#include "value.h"
#include "object.h"
#include "map.h"

#include <time.h> // for CLOCKS_PER_SEC, don't remove it
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

/* ── natives -- */

typedef struct { char* buf; int len; int cap; } StrBuf;

static void sb_append(StrBuf* sb, const char* s, int n) {
    if (sb->len + n + 1 > sb->cap) {
        int cap = sb->cap ? sb->cap : 64;
        while (cap < sb->len + n + 1) cap *= 2;
        sb->buf = (char*)realloc(sb->buf, cap);
        if (sb->buf == NULL) exit(1);
        sb->cap = cap;
    }
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

// a collection inside itself is written as [...] or {...}, like print does
static void sb_value(StrBuf* sb, Value v) {
    char tmp[64];
    if (IS_NUMBER(v))      { int n = snprintf(tmp, sizeof tmp, "%g", AS_NUMBER(v)); sb_append(sb, tmp, n); }
    else if (IS_BOOL(v))   { if (AS_BOOL(v)) sb_append(sb, "true", 4); else sb_append(sb, "false", 5); }
    else if (IS_NIL(v))    { sb_append(sb, "nil", 3); }
    else if (IS_STRING(v)) { sb_append(sb, AS_CSTRING(v), AS_STRING(v)->length); }
    else if (IS_ARRAY(v)) {
        if (!print_enter(AS_OBJ(v))) { sb_append(sb, "[...]", 5); return; }
        ObjArray* a = AS_ARRAY(v);
        sb_append(sb, "[", 1);
        for (int i = 0; i < a->items.count; i++) {
            if (i > 0) sb_append(sb, ", ", 2);
            sb_value(sb, a->items.values[i]);
        }
        sb_append(sb, "]", 1);
        print_leave();
    }
    else if (IS_MAP(v)) {
        if (!print_enter(AS_OBJ(v))) { sb_append(sb, "{...}", 5); return; }
        ObjMap* m = AS_MAP(v);
        bool first = true;
        sb_append(sb, "{", 1);
        for (int i = 0; i < m->count; i++) {
            if (IS_NIL(m->entries[i].key)) continue;   // removed
            if (!first) sb_append(sb, ", ", 2);
            first = false;
            sb_value(sb, m->entries[i].key);
            sb_append(sb, ": ", 2);
            sb_value(sb, m->entries[i].value);
        }
        sb_append(sb, "}", 1);
        print_leave();
    }
    else sb_append(sb, "<object>", 8);
}

static Value clock_native(int argc, Value* args) { return NUMBER_VAL((double)clock()/CLOCKS_PER_SEC); }
static Value sqrt_native(int argc, Value* args)  { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(sqrt(AS_NUMBER(args[0]))); }
static Value abs_native(int argc, Value* args)   { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(fabs(AS_NUMBER(args[0]))); }
static Value floor_native(int argc, Value* args) { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(floor(AS_NUMBER(args[0]))); }
static Value ceil_native(int argc, Value* args)  { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(ceil(AS_NUMBER(args[0]))); }
static Value sin_native(int argc, Value* args)   { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(sin(AS_NUMBER(args[0]))); }
static Value cos_native(int argc, Value* args)   { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(cos(AS_NUMBER(args[0]))); }
static Value tan_native(int argc, Value* args)   { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(tan(AS_NUMBER(args[0]))); }
static Value atan2_native(int argc, Value* args) { if(argc!=2||!IS_NUMBER(args[0])||!IS_NUMBER(args[1]))return NIL_VAL; return NUMBER_VAL(atan2(AS_NUMBER(args[0]),AS_NUMBER(args[1]))); }
static Value pow_native(int argc, Value* args)   { if(argc!=2||!IS_NUMBER(args[0])||!IS_NUMBER(args[1]))return NIL_VAL; return NUMBER_VAL(pow(AS_NUMBER(args[0]),AS_NUMBER(args[1]))); }
static Value min_native(int argc, Value* args)   { if(argc!=2||!IS_NUMBER(args[0])||!IS_NUMBER(args[1]))return NIL_VAL; return AS_NUMBER(args[0])<AS_NUMBER(args[1])?args[0]:args[1]; }
static Value max_native(int argc, Value* args)   { if(argc!=2||!IS_NUMBER(args[0])||!IS_NUMBER(args[1]))return NIL_VAL; return AS_NUMBER(args[0])>AS_NUMBER(args[1])?args[0]:args[1]; }
//static Value len_native(int argc, Value* args)   { if(argc!=1||!IS_STRING(args[0]))return NIL_VAL; return NUMBER_VAL((double)AS_STRING(args[0])->length); }
static Value len_native(int argc, Value* args) {
    if (argc != 1) return NIL_VAL;
    if (IS_STRING(args[0])) return NUMBER_VAL((double)AS_STRING(args[0])->length);
    if (IS_ARRAY(args[0]))  return NUMBER_VAL((double)AS_ARRAY(args[0])->items.count);
    if (IS_MAP(args[0]))    return NUMBER_VAL((double)AS_MAP(args[0])->live);
    return NIL_VAL;
}

/* ── maps have no methods: these are plain functions ── */

static Value has_native(int argc, Value* args) {
    if (argc != 2 || !IS_MAP(args[0]) || map_key_error(args[1]) != NULL) return NIL_VAL;
    Value v;
    return BOOL_VAL(map_get(AS_MAP(args[0]), args[1], &v));
}

static Value keys_native(int argc, Value* args) {
    if (argc != 1 || !IS_MAP(args[0])) return NIL_VAL;
    ObjMap* m = AS_MAP(args[0]);
    ObjArray* keys = new_array();           // can GC; the map is safe in args, on the stack
    for (int i = 0; i < m->count; i++) {
        if (!IS_NIL(m->entries[i].key)) write_value_array(&keys->items, m->entries[i].key);
    }
    return OBJ_VAL(keys);
}

static Value remove_native(int argc, Value* args) {
    if (argc != 2 || !IS_MAP(args[0]) || map_key_error(args[1]) != NULL) return NIL_VAL;
    Value v;
    if (!map_remove(AS_MAP(args[0]), args[1], &v)) return NIL_VAL;
    return v;                               // the removed value, like pop
}



static Value push_native(int argc, Value* args) {
    if (argc != 2 || !IS_ARRAY(args[0])) return NIL_VAL;
    write_value_array(&AS_ARRAY(args[0])->items, args[1]);
    return args[0];                       // returns the array, so calls chain
}

static Value pop_native(int argc, Value* args) {
    if (argc != 1 || !IS_ARRAY(args[0])) return NIL_VAL;
    ObjArray* a = AS_ARRAY(args[0]);
    if (a->items.count == 0) return NIL_VAL;
    return a->items.values[--a->items.count];
}

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
    else if (IS_ARRAY(args[0]) || IS_MAP(args[0])) {
        StrBuf sb = {0};
        sb_value(&sb, args[0]);
        ObjString* s = copy_str(sb.buf, sb.len);
        free(sb.buf);
        return OBJ_VAL(s);
    }
    else return NIL_VAL;
    return OBJ_VAL(copy_str(buf,len));
}

static Value random_native(int argc, Value* args) {
    
    if(argc!=2  ||  !IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        return NIL_VAL;
    }

    int min = AS_NUMBER(args[0]);
    int max = AS_NUMBER(args[1]);

    if (min > max) { int t = min; min = max; max = t; }

    long long range = (long long) max - min + 1;
    int rand_val = (int) (rand() % range) + min;

    return NUMBER_VAL(rand_val);

}

void register_std_natives() {
    srand((unsigned) time(NULL));

    define_native("clock",clock_native);
    define_native("sqrt",sqrt_native);
    define_native("abs",abs_native);
    define_native("floor",floor_native);
    define_native("ceil",ceil_native);
    define_native("sin",sin_native);
    define_native("cos",cos_native);
    define_native("tan",tan_native);
    define_native("atan2",atan2_native);
    define_native("pow",pow_native);
    define_native("min",min_native);
    define_native("max",max_native);
    define_native("str",str_native);
    define_native("len",len_native);
    define_native("hex",hex_native);
    define_native("random",random_native);
    define_native("push", push_native);
    define_native("pop",  pop_native);
    define_native("has",    has_native);
    define_native("keys",   keys_native);
    define_native("remove", remove_native);
}

