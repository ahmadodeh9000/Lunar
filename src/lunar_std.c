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

static void sb_cstr(StrBuf* sb, const char* s) { sb_append(sb, s, (int)strlen(s)); }

static void sb_function(StrBuf* sb, ObjFunction* fn) {
    if (fn->name == NULL) { sb_cstr(sb, "<script>"); return; }
    sb_cstr(sb, "<fn "); sb_append(sb, fn->name->chars, fn->name->length); sb_cstr(sb, ">");
}

// the other objects, written the way printObject writes them
static void sb_object(StrBuf* sb, Value v) {
    switch (OBJ_TYPE(v)) {
        case OBJ_FUNCTION:     sb_function(sb, AS_FUNCTION(v)); break;
        case OBJ_CLOSURE:      sb_function(sb, AS_CLOSURE(v)->function); break;
        case OBJ_BOUND_METHOD: sb_function(sb, AS_BOUND_METHOD(v)->method->function); break;
        case OBJ_NATIVE:
            sb_cstr(sb, "<native fn "); sb_cstr(sb, ((ObjNative*)AS_OBJ(v))->name); sb_cstr(sb, ">");
            break;
        case OBJ_UPVALUE:      sb_cstr(sb, "<upvalue>"); break;
        case OBJ_STRUCT:
            sb_cstr(sb, "<struct "); sb_cstr(sb, AS_STRUCT(v)->name->chars); sb_cstr(sb, ">");
            break;
        case OBJ_INSTANCE:
            sb_cstr(sb, "<instance of "); sb_cstr(sb, AS_INSTANCE(v)->klass->name->chars); sb_cstr(sb, ">");
            break;
        case OBJ_FFI_LIB:
            sb_cstr(sb, "<ffi library '"); sb_cstr(sb, AS_FFI_LIB(v)->path->chars); sb_cstr(sb, "'>");
            break;
        case OBJ_FFI_FUNC:
            sb_cstr(sb, "<ffi native fn '"); sb_cstr(sb, AS_FFI_FUNC(v)->name->chars); sb_cstr(sb, "'>");
            break;
        default: break;     // strings, arrays, and maps are handled by sb_value
    }
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
    else sb_object(sb, v);
}

char* value_text(Value v, int* length) {
    StrBuf sb = {0};
    sb_value(&sb, v);
    if (sb.buf == NULL) sb_append(&sb, "", 0);
    *length = sb.len;
    return sb.buf;
}

/* ── argument checks ── */

const char* value_type_name(Value v) {
    if (IS_NUMBER(v)) return "number";
    if (IS_BOOL(v))   return "bool";
    if (IS_NIL(v))    return "nil";
    switch (OBJ_TYPE(v)) {
        case OBJ_STRING:       return "string";
        case OBJ_ARRAY:        return "array";
        case OBJ_MAP:          return "map";
        case OBJ_STRUCT:       return "struct";
        case OBJ_INSTANCE:     return "instance";
        case OBJ_FFI_LIB:      return "FFI library";
        case OBJ_FUNCTION: case OBJ_NATIVE: case OBJ_CLOSURE:
        case OBJ_BOUND_METHOD: case OBJ_FFI_FUNC:
                               return "function";
        case OBJ_UPVALUE:      return "upvalue";
    }
    return "value";
}

bool check_arity(const char* fn, int argc, int want) {
    if (argc == want) return true;
    runtime_error("%s() takes %d argument%s (got %d).", fn, want, want == 1 ? "" : "s", argc);
    return false;
}

// want reads after "expects", e.g. "a number"
bool check_arg(const char* fn, Value* args, int i, bool ok, const char* want) {
    if (ok) return true;
    runtime_error("%s() expects %s for argument %d (got %s).", fn, want, i + 1, value_type_name(args[i]));
    return false;
}

bool check_numbers(const char* fn, int argc, Value* args, int want) {
    if (!check_arity(fn, argc, want)) return false;
    for (int i = 0; i < want; i++) {
        if (!check_arg(fn, args, i, IS_NUMBER(args[i]), "a number")) return false;
    }
    return true;
}

static Value clock_native(int argc, Value* args) { return NUMBER_VAL((double)clock()/CLOCKS_PER_SEC); }
static Value sqrt_native(int argc, Value* args)  { if(!check_numbers("sqrt",argc,args,1))return NIL_VAL; return NUMBER_VAL(sqrt(AS_NUMBER(args[0]))); }
static Value abs_native(int argc, Value* args)   { if(!check_numbers("abs",argc,args,1))return NIL_VAL; return NUMBER_VAL(fabs(AS_NUMBER(args[0]))); }
static Value floor_native(int argc, Value* args) { if(!check_numbers("floor",argc,args,1))return NIL_VAL; return NUMBER_VAL(floor(AS_NUMBER(args[0]))); }
static Value ceil_native(int argc, Value* args)  { if(!check_numbers("ceil",argc,args,1))return NIL_VAL; return NUMBER_VAL(ceil(AS_NUMBER(args[0]))); }
static Value sin_native(int argc, Value* args)   { if(!check_numbers("sin",argc,args,1))return NIL_VAL; return NUMBER_VAL(sin(AS_NUMBER(args[0]))); }
static Value cos_native(int argc, Value* args)   { if(!check_numbers("cos",argc,args,1))return NIL_VAL; return NUMBER_VAL(cos(AS_NUMBER(args[0]))); }
static Value tan_native(int argc, Value* args)   { if(!check_numbers("tan",argc,args,1))return NIL_VAL; return NUMBER_VAL(tan(AS_NUMBER(args[0]))); }
static Value atan2_native(int argc, Value* args) { if(!check_numbers("atan2",argc,args,2))return NIL_VAL; return NUMBER_VAL(atan2(AS_NUMBER(args[0]),AS_NUMBER(args[1]))); }
static Value pow_native(int argc, Value* args)   { if(!check_numbers("pow",argc,args,2))return NIL_VAL; return NUMBER_VAL(pow(AS_NUMBER(args[0]),AS_NUMBER(args[1]))); }
static Value min_native(int argc, Value* args)   { if(!check_numbers("min",argc,args,2))return NIL_VAL; return AS_NUMBER(args[0])<AS_NUMBER(args[1])?args[0]:args[1]; }
static Value max_native(int argc, Value* args)   { if(!check_numbers("max",argc,args,2))return NIL_VAL; return AS_NUMBER(args[0])>AS_NUMBER(args[1])?args[0]:args[1]; }
//static Value len_native(int argc, Value* args)   { if(argc!=1||!IS_STRING(args[0]))return NIL_VAL; return NUMBER_VAL((double)AS_STRING(args[0])->length); }
static Value len_native(int argc, Value* args) {
    if (!check_arity("len", argc, 1)) return NIL_VAL;
    if (IS_STRING(args[0])) return NUMBER_VAL((double)AS_STRING(args[0])->length);
    if (IS_ARRAY(args[0]))  return NUMBER_VAL((double)AS_ARRAY(args[0])->items.count);
    if (IS_MAP(args[0]))    return NUMBER_VAL((double)AS_MAP(args[0])->live);
    check_arg("len", args, 0, false, "a string, array, or map");
    return NIL_VAL;
}

/* ── maps have no methods: these are plain functions ── */

// the map and key arguments of has and remove
static bool check_map_key(const char* fn, int argc, Value* args) {
    if (!check_arity(fn, argc, 2)) return false;
    if (!check_arg(fn, args, 0, IS_MAP(args[0]), "a map")) return false;
    const char* err = map_key_error(args[1]);
    if (err != NULL) { runtime_error("%s", err); return false; }
    return true;
}

static Value has_native(int argc, Value* args) {
    if (!check_map_key("has", argc, args)) return NIL_VAL;
    Value v;
    return BOOL_VAL(map_get(AS_MAP(args[0]), args[1], &v));
}

static Value keys_native(int argc, Value* args) {
    if (!check_arity("keys", argc, 1) || !check_arg("keys", args, 0, IS_MAP(args[0]), "a map")) return NIL_VAL;
    ObjMap* m = AS_MAP(args[0]);
    ObjArray* keys = new_array();           // can GC; the map is safe in args, on the stack
    for (int i = 0; i < m->count; i++) {
        if (!IS_NIL(m->entries[i].key)) write_value_array(&keys->items, m->entries[i].key);
    }
    return OBJ_VAL(keys);
}

static Value remove_native(int argc, Value* args) {
    if (!check_map_key("remove", argc, args)) return NIL_VAL;
    Value v;
    if (!map_remove(AS_MAP(args[0]), args[1], &v)) return NIL_VAL;
    return v;                               // the removed value, like pop
}



static Value push_native(int argc, Value* args) {
    if (!check_arity("push", argc, 2) || !check_arg("push", args, 0, IS_ARRAY(args[0]), "an array")) return NIL_VAL;
    write_value_array(&AS_ARRAY(args[0])->items, args[1]);
    return args[0];                       // returns the array, so calls chain
}

static Value pop_native(int argc, Value* args) {
    if (!check_arity("pop", argc, 1) || !check_arg("pop", args, 0, IS_ARRAY(args[0]), "an array")) return NIL_VAL;
    ObjArray* a = AS_ARRAY(args[0]);
    if (a->items.count == 0) return NIL_VAL;
    return a->items.values[--a->items.count];
}

static Value hex_native(int argc, Value* args) {
    if (!check_arity("hex", argc, 1) || !check_arg("hex", args, 0, IS_STRING(args[0]), "a string")) {
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
// the text print shows, for any value
static Value str_native(int argc, Value* args) {
    if(!check_arity("str",argc,1)) return NIL_VAL;
    if(IS_STRING(args[0])) return args[0];
    int len;
    char* text=value_text(args[0],&len);
    ObjString* s=copy_str(text,len);
    free(text);
    return OBJ_VAL(s);
}

static Value random_native(int argc, Value* args) {
    
    if(!check_numbers("random",argc,args,2)) {
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

