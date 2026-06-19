#include "memory.h"
#include "object.h"
#include "vm.h"
#include "table.h"
#include "compiler.h"

#include <stdlib.h>
#include <stdio.h>
#if __has_include(<ffi.h>)
#include <ffi.h>
#elif __has_include(<ffi/ffi.h>)
#include <ffi/ffi.h>
#else
#error "libffi header not found"
#endif

#ifndef _WIN32
#include <dlfcn.h>  // FIX SHIT
#endif

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* pointer, size_t old_size, size_t new_size) {

    lvm.bytes_allocated += new_size - old_size;

    if (new_size > old_size) {
#ifdef LUNAR_DEBUG_STRESS_GC
        collect_garbage();
#else
        if (lvm.bytes_allocated > lvm.next_gc) {
            collect_garbage();
        }
#endif
    }

    if (new_size == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, new_size);
    if (NULL == result) {
        fprintf(stderr, "OUT OF MEMORY !\n");
        exit(-1);
    }
    return result;
}

/* ───────── Mark phase ───────── */

void mark_object(Obj* object) {
    if (object == NULL)       return;
    if (object->is_marked)    return;

#ifdef LUNAR_DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    object->is_marked = true;

    // push onto gray stack for later traversal
    if (lvm.gray_count >= lvm.gray_capacity) {
        lvm.gray_capacity = GROW_CAPACITY(lvm.gray_capacity);
        lvm.gray_stack    = (Obj**)realloc(lvm.gray_stack,
                                sizeof(Obj*) * lvm.gray_capacity);
        if (lvm.gray_stack == NULL) exit(1);
    }
    lvm.gray_stack[lvm.gray_count++] = object;
}

void mark_value(Value value) {
    if (IS_OBJ(value)) mark_object(AS_OBJ(value));
}

static void mark_array(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        mark_value(array->values[i]);
    }
}

static void mark_table(Table* table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        mark_object((Obj*)entry->key);
        mark_value(entry->value);
    }
}

static void mark_roots() {
    // stack values
    for (Value* slot = lvm.stack; slot < lvm.stack_top; slot++) {
        mark_value(*slot);
    }

    // call frames (closures)
    for (int i = 0; i < lvm.frame_count; i++) {
        mark_object((Obj*)lvm.frames[i].closure);
    }

    // open upvalues
    for (ObjUpvalue* uv = lvm.open_upvalues; uv != NULL; uv = uv->next) {
        mark_object((Obj*)uv);
    }

    // globals
    mark_table(&lvm.globals);

    // compiler roots
    mark_compiler_roots();

    // init string
    mark_object((Obj*)lvm.init_string);
}

/* ───────── Trace (blacken) phase ───────── */

static void blacken_object(Obj* object) {
#ifdef LUNAR_DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_UPVALUE:
            mark_value(((ObjUpvalue*)object)->closed);
            break;

        case OBJ_FUNCTION: {
            ObjFunction* fn = (ObjFunction*)object;
            mark_object((Obj*)fn->name);
            mark_array(&fn->chunk.constants);
            break;
        }

        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            mark_object((Obj*)closure->function);
            for (int i = 0; i < closure->upvalue_count; i++) {
                mark_object((Obj*)closure->upvalues[i]);
            }
            break;
        }

        case OBJ_STRUCT: {
            ObjStruct* klass = (ObjStruct*)object;
            mark_object((Obj*)klass->name);
            mark_table(klass->methods);
            break;
        }

        case OBJ_INSTANCE: {
            ObjInstance* inst = (ObjInstance*)object;
            mark_object((Obj*)inst->klass);
            mark_table(inst->fields);
            break;
        }

        case OBJ_BOUND_METHOD: {
            ObjBoundMethod* bm = (ObjBoundMethod*)object;
            mark_value(bm->receiver);
            mark_object((Obj*)bm->method);
            break;
        }

        case OBJ_NATIVE:
        case OBJ_STRING:
            break; // no outgoing references
// don't touch this shit please ahmad  - ahmad
        case OBJ_FFI_LIB: {
            ObjFFILib* lib = (ObjFFILib*)object;
            mark_object((Obj*)lib->path); // Fixed typo here
            break;
        }
        case OBJ_FFI_FUNC: {
            ObjFFIFunc* func = (ObjFFIFunc*)object;
            mark_object((Obj*)func->name); // Fixed typo here
            break;
        }
    }
}

static void trace_references() {
    while (lvm.gray_count > 0) {
        Obj* object = lvm.gray_stack[--lvm.gray_count];
        blacken_object(object);
    }
}

/* ───────── Sweep phase ───────── */

static void table_remove_white(Table* table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.is_marked) {
            table_delete(table, entry->key);
        }
    }
}

static void sweep() {
    Obj* previous = NULL;
    Obj* object   = lvm.objects;

    while (object != NULL) {
        if (object->is_marked) {
            object->is_marked = false;  // reset for next cycle
            previous = object;
            object   = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;

            if (previous != NULL) {
                previous->next = object;
            } else {
                lvm.objects = object;
            }

            freeObject(unreached);
        }
    }
}

void collect_garbage() {
#ifdef LUNAR_DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = lvm.bytes_allocated;
#endif

    mark_roots();
    trace_references();
    table_remove_white(&lvm.strings); // weak references to interned strings
    sweep();

    lvm.next_gc = lvm.bytes_allocated * GC_HEAP_GROW_FACTOR;

#ifdef LUNAR_DEBUG_LOG_GC
    printf("-- gc end  (collected %zu bytes, %zu remaining)\n",
           before - lvm.bytes_allocated, lvm.bytes_allocated);
#endif
}

/* ───────── Free individual object ───────── */

void freeObject(Obj* object) {
#ifdef LUNAR_DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* fn = (ObjFunction*)object;
            free_chunk(&fn->chunk);
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_NATIVE:
            FREE(ObjNative, object);
            break;
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalue_count);
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_UPVALUE:
            FREE(ObjUpvalue, object);
            break;
        case OBJ_STRUCT: {
            ObjStruct* klass = (ObjStruct*)object;
            free_table(klass->methods);
            FREE(Table, klass->methods);
            FREE(ObjStruct, object);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* inst = (ObjInstance*)object;
            free_table(inst->fields);
            FREE(Table, inst->fields);
            FREE(ObjInstance, object);
            break;
        }
        case OBJ_BOUND_METHOD:
            FREE(ObjBoundMethod, object);
            break;

        case OBJ_FFI_LIB: {
            ObjFFILib* lib = (ObjFFILib*)object;
#ifdef _WIN32
            FreeLibrary((HMODULE)lib->handle);
#else
            dlclose(lib->handle); // Cleaned up the placeholder function call
#endif
            FREE(ObjFFILib, object);
            break;
        }
        case OBJ_FFI_FUNC: {
            ObjFFIFunc* func = (ObjFFIFunc*)object;
            // Free the arrays allocated during ffiBind
            FREE_ARRAY(ffi_type*, func->arg_types, func->arg_count);
            FREE_ARRAY(int, func->lunar_arg_types, func->arg_count);
            FREE(ObjFFIFunc, object);
            break;
        }
    }
}

void freeObjects() {
    Obj* object = lvm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
    free(lvm.gray_stack);
}
