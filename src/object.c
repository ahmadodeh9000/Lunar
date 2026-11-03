#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include "memory.h"
#include "object.h"
#include "value.h"
#include "table.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocate_object(sizeof(type), objectType)


static Obj* allocate_object(size_t size, ObjType type) {
    Obj* object     = (Obj*)reallocate(NULL, 0, size);
    object->type    = type;
    object->is_marked = false;
    object->next    = lvm.objects;
    lvm.objects     = object;

#ifdef LUNAR_DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif

    return object;
}

/* ── FNV-1a hash ── */
static u32 hash_string(const char* key, int length) {
    u32 hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (u8)key[i];
        hash *= 16777619;
    }
    return hash;
}
/*
static ObjString* allocate_string(char* chars, int length, u32 hash) {
    ObjString* string   = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length      = length;
    string->chars       = chars;
    string->hash        = hash;
    // intern the string
    table_set(&lvm.strings, string, NIL_VAL);
    return string;
}
*/
static ObjString* allocate_string(char* chars, int length, u32 hash) {
    ObjString* string   = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length      = length;
    string->chars       = chars;
    string->hash        = hash;
    push(OBJ_VAL(string));                          // add: root it during the insert
    table_set(&lvm.strings, string, NIL_VAL);
    pop();                                          // add
    return string;
}

ObjString* copy_str(const char* chars, int length) {
    u32 hash        = hash_string(chars, length);
    ObjString* interned = table_find_string(&lvm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    char* heap = ALLOCATE(char, length + 1);
    memcpy(heap, chars, length);
    heap[length] = '\0';
    return allocate_string(heap, length, hash);
}

ObjString* take_str(char* chars, int length) {
    u32 hash        = hash_string(chars, length);
    ObjString* interned = table_find_string(&lvm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return allocate_string(chars, length, hash);
}

ObjFunction* new_function() {
    ObjFunction* fn  = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    fn->arity        = 0;
    fn->upvalue_count = 0;
    fn->name         = NULL;
    fn->module       = NULL;
    init_chunk(&fn->chunk);
    return fn;
}

ObjNative* new_native(NativeFn fn, const char* name) {
    ObjNative* native   = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function    = fn;
    native->name        = name;
    return native;
}

ObjClosure* new_closure(ObjFunction* function) {
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalue_count);
    for (int i = 0; i < function->upvalue_count; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure* closure     = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function       = function;
    closure->upvalues       = upvalues;
    closure->upvalue_count  = function->upvalue_count;
    return closure;
}

ObjUpvalue* new_upvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location   = slot;
    upvalue->closed     = NIL_VAL;
    upvalue->next       = NULL;
    return upvalue;
}

ObjStruct* new_struct(ObjString* name) {
    // Allocate and init the table BEFORE the object: ALLOCATE can trigger a GC,
    // and an unrooted half-built object would be swept (and its garbage
    // `methods` pointer freed).
    Table* methods = ALLOCATE(Table, 1);
    init_table(methods);
    ObjStruct* klass = ALLOCATE_OBJ(ObjStruct, OBJ_STRUCT);
    klass->name      = name;
    klass->methods   = methods;
    return klass;
}

ObjInstance* new_instance(ObjStruct* klass) {
    Table* fields = ALLOCATE(Table, 1);   // before the object, see new_struct
    init_table(fields);
    ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->klass       = klass;
    instance->fields      = fields;
    return instance;
}

ObjBoundMethod* new_bound_method(Value receiver, ObjClosure* method) {
    ObjBoundMethod* bm = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
    bm->receiver       = receiver;
    bm->method         = method;
    return bm;
}

ObjArray* new_array() {
    ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
    init_value_array(&array->items);
    return array;
}

ObjMap* new_map() {
    ObjMap* map         = ALLOCATE_OBJ(ObjMap, OBJ_MAP);
    map->entries        = NULL;
    map->count          = 0;
    map->capacity       = 0;
    map->live           = 0;
    map->index          = NULL;
    map->index_capacity = 0;
    return map;
}

ObjModule* new_module(ObjString* file, ObjString* name) {
    ObjModule* module = ALLOCATE_OBJ(ObjModule, OBJ_MODULE);
    module->file      = file;
    module->name      = name;
    init_table(&module->globals);
    module->exports   = NIL_VAL;
    module->loaded    = false;
    return module;
}

ObjFFILib* new_ffi_lib(ObjString* path, void* handle) {
    ObjFFILib* lib = ALLOCATE_OBJ(ObjFFILib, OBJ_FFI_LIB);
    lib->path = path;
    lib->handle = handle;
    return lib;
}

ObjFFIFunc* new_ffi_func(ObjString* name, void* symbol) {
    ObjFFIFunc* func = ALLOCATE_OBJ(ObjFFIFunc, OBJ_FFI_FUNC);
    func->name = name;
    func->func_ptr = symbol;
    func->arg_count = 0;
    func->arg_types = NULL;
    func->lunar_arg_types = NULL;
    func->rtype = NULL;
    func->lunar_return_type = 0;
    return func;
}

/* ── cycle check for printing: the arrays and maps currently being printed ── */
static Obj** printing          = NULL;
static int   printing_count    = 0;
static int   printing_capacity = 0;

bool print_enter(Obj* collection) {
    for (int i = 0; i < printing_count; i++) {
        if (printing[i] == collection) return false;
    }
    if (printing_count == printing_capacity) {
        printing_capacity = printing_capacity < 8 ? 8 : 2 * printing_capacity;
        printing = (Obj**)realloc(printing, sizeof(Obj*) * printing_capacity);
        if (printing == NULL) { fprintf(stderr, "OOM print\n"); exit(1); }
    }
    printing[printing_count++] = collection;
    return true;
}

void print_leave() {
    if (--printing_count == 0) {
        free(printing);
        printing = NULL;
        printing_capacity = 0;
    }
}

static void print_function(ObjFunction* fn) {
    if (fn->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", fn->name->chars);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_FUNCTION:
            print_function(AS_FUNCTION(value));
            break;
        case OBJ_NATIVE:
            printf("<native fn %s>", ((ObjNative*)AS_OBJ(value))->name);
            break;
        case OBJ_CLOSURE:
            print_function(AS_CLOSURE(value)->function);
            break;
        case OBJ_UPVALUE:
            printf("<upvalue>");
            break;
        case OBJ_STRUCT:
            printf("<struct %s>", AS_STRUCT(value)->name->chars);
            break;
        case OBJ_INSTANCE:
            printf("<instance of %s>", AS_INSTANCE(value)->klass->name->chars);
            break;
        case OBJ_BOUND_METHOD:
            print_function(AS_BOUND_METHOD(value)->method->function);
            break;


        case OBJ_FFI_LIB:
            printf("<ffi library '%s'>", AS_FFI_LIB(value)->path->chars);
            break;
        case OBJ_FFI_FUNC:
            printf("<ffi native fn '%s'>", AS_FFI_FUNC(value)->name->chars);
            break;


        case OBJ_MODULE:
            printf("<module %s>", AS_MODULE(value)->name->chars);
            break;

        case OBJ_ARRAY: {
            ObjArray* a = AS_ARRAY(value);
            if (!print_enter(AS_OBJ(value))) { printf("[...]"); break; }
            printf("[");
            for (int i = 0; i < a->items.count; i++) {
                if (i > 0) printf(", ");
                printValue(a->items.values[i]);
            }
            printf("]");
            print_leave();
            break;
        }

        case OBJ_MAP: {
            ObjMap* m = AS_MAP(value);
            if (!print_enter(AS_OBJ(value))) { printf("{...}"); break; }
            printf("{");
            bool first = true;
            for (int i = 0; i < m->count; i++) {
                if (IS_NIL(m->entries[i].key)) continue;   // removed
                if (!first) printf(", ");
                first = false;
                printValue(m->entries[i].key);
                printf(": ");
                printValue(m->entries[i].value);
            }
            printf("}");
            print_leave();
            break;
        }

    }
}
