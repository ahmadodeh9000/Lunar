
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "lunar_ffi.h"
#include "vm.h"
#include "object.h"
#include "memory.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if __has_include(<ffi.h>)
#include <ffi.h>
#elif __has_include(<ffi/ffi.h>)
#include <ffi/ffi.h>
#else
#error "libffi header not found"
#endif

// Maps human-readable strings from scripts to libffi types and our structural validation types
static bool parse_type(ObjString* typeStr, ffi_type** ffiOut, int* lunarOut) {
    if (strcmp(typeStr->chars, "i32") == 0 || strcmp(typeStr->chars, "int") == 0) {
        *ffiOut = &ffi_type_sint32;
        *lunarOut = LUNAR_FFI_INT;
        return true;
    } else if (strcmp(typeStr->chars, "double") == 0) {
        *ffiOut = &ffi_type_double;
        *lunarOut = LUNAR_FFI_DOUBLE;
        return true;
    } else if (strcmp(typeStr->chars, "string") == 0) {
        *ffiOut = &ffi_type_pointer;
        *lunarOut = LUNAR_FFI_STRING;
        return true;
    } else if (strcmp(typeStr->chars, "void") == 0) {
        *ffiOut = &ffi_type_void;
        *lunarOut = LUNAR_FFI_VOID;
        return true;
    }
    return false;
}

/* ─────────────────────────────────────────────────────────────
   Native Function: ffi_load_library
   Usage in Script: let lib = clib("libc.so.6");
   ───────────────────────────────────────────────────────────-─ */
Value ffi_load_library(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        return NIL_VAL;
    }

    ObjString* path = AS_STRING(args[0]);
    void* handle = NULL;

#ifdef _WIN32
    handle = LoadLibraryA(path->chars);
#else
    // If path is empty string, pass NULL to dlopen to search the main executable/loaded shared libs
    handle = dlopen(path->length == 0 ? NULL : path->chars, RTLD_LAZY);
#endif

    if (!handle) {
        return NIL_VAL;
    }

    // Single allocation, returned immediately with no further heap activity
    // in between, so no GC-protection push/pop needed here.
    // Note : I SPEND A FUCKING DAY FIGHT WITH THIS SHIT
    ObjFFILib* lib = new_ffi_lib(path, handle);
    return OBJ_VAL(lib);
}


Value ffi_bind_function(int argCount, Value* args) {
    if (argCount < 3 || !IS_FFI_LIB(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2])) {
        return NIL_VAL;
    }

    ObjFFILib* lib = AS_FFI_LIB(args[0]);
    ObjString* funcName = AS_STRING(args[1]);
    ObjString* returnTypeStr = AS_STRING(args[2]);
    int param_count = argCount - 3;

    void* symbol = NULL;
#ifdef _WIN32
    symbol = GetProcAddress((HMODULE)lib->handle, funcName->chars);
#else
    symbol = dlsym(lib->handle, funcName->chars);
#endif
    if (!symbol) {
        return NIL_VAL;
    }

    ffi_type* rtype_scratch;
    int lunar_rtype_scratch;
    if (!parse_type(returnTypeStr, &rtype_scratch, &lunar_rtype_scratch)) {
        return NIL_VAL;
    }

    ffi_type** arg_types_scratch = NULL;
    int* lunar_arg_types_scratch = NULL;

    if (param_count > 0) {
        arg_types_scratch = malloc(sizeof(ffi_type*) * (size_t)param_count);
        lunar_arg_types_scratch = malloc(sizeof(int) * (size_t)param_count);
        if (!arg_types_scratch || !lunar_arg_types_scratch) {
            free(arg_types_scratch);
            free(lunar_arg_types_scratch);
            return NIL_VAL;
        }

        for (int i = 0; i < param_count; i++) {
            Value typeVal = args[3 + i];
            if (!IS_STRING(typeVal) ||
                !parse_type(AS_STRING(typeVal), &arg_types_scratch[i], &lunar_arg_types_scratch[i])) {
                free(arg_types_scratch);
                free(lunar_arg_types_scratch);
                return NIL_VAL;
            }


            if (lunar_arg_types_scratch[i] == LUNAR_FFI_VOID) {
                free(arg_types_scratch);
                free(lunar_arg_types_scratch);
                return NIL_VAL;
            }
        }
    }

    // --- Phase 2: everything validated, safe to touch the GC heap now ---
    ObjFFIFunc* func = new_ffi_func(funcName, symbol);
    push(OBJ_VAL(func)); // protect from GC for the rest of this function

    func->rtype = rtype_scratch;
    func->lunar_return_type = lunar_rtype_scratch;
    func->arg_count = param_count;

    if (param_count > 0) {
        func->arg_types = ALLOCATE(ffi_type*, param_count);
        func->lunar_arg_types = ALLOCATE(int, param_count);
        memcpy(func->arg_types, arg_types_scratch, sizeof(ffi_type*) * (size_t)param_count);
        memcpy(func->lunar_arg_types, lunar_arg_types_scratch, sizeof(int) * (size_t)param_count);
        free(arg_types_scratch);
        free(lunar_arg_types_scratch);
    } else {
        func->arg_types = NULL;
        func->lunar_arg_types = NULL;
    }

    if (ffi_prep_cif(&func->cif, FFI_DEFAULT_ABI, func->arg_count, func->rtype, func->arg_types) != FFI_OK) {

        
        if (func->arg_count > 0) {
            FREE_ARRAY(ffi_type*, func->arg_types, func->arg_count);
            FREE_ARRAY(int, func->lunar_arg_types, func->arg_count);
        }
        func->arg_types = NULL;
        func->lunar_arg_types = NULL;
        pop();
        return NIL_VAL;
    }

    pop();
    return OBJ_VAL(func);
}

Value ffi_call_bound(ObjFFIFunc* func, int argCount, Value* args) {
    if (argCount != func->arg_count) {
        fprintf(stderr, "FFI Runtime Error: Function '%s' expected %d arguments, got %d.\n",
                func->name->chars, func->arg_count, argCount);
        return NIL_VAL;
    }

    for (int i = 0; i < func->arg_count; i++) {
        Value val = args[i];
        switch (func->lunar_arg_types[i]) {
            case LUNAR_FFI_INT:
            case LUNAR_FFI_DOUBLE:
                if (!IS_NUMBER(val)) {
                    fprintf(stderr, "FFI Runtime Error: '%s' expected a number for argument %d.\n",
                            func->name->chars, i + 1);
                    return NIL_VAL;
                }
                break;
            case LUNAR_FFI_STRING:
                if (!IS_STRING(val)) {
                    fprintf(stderr, "FFI Runtime Error: '%s' expected a string for argument %d.\n",
                            func->name->chars, i + 1);
                    return NIL_VAL;
                }
                break;
            default:
                break;
        }
    }

    void** ffi_values = NULL;
    void** memory_allocations = NULL;

    if (func->arg_count > 0) {
        ffi_values = malloc(sizeof(void*) * (size_t)func->arg_count);
        memory_allocations = malloc(sizeof(void*) * (size_t)func->arg_count);
        if (!ffi_values || !memory_allocations) {
            free(ffi_values);
            free(memory_allocations);
            fprintf(stderr, "FFI Runtime Error: out of memory binding arguments for '%s'.\n", func->name->chars);
            return NIL_VAL;
        }
    }

    for (int i = 0; i < func->arg_count; i++) {
        Value val = args[i];

        switch (func->lunar_arg_types[i]) {
            case LUNAR_FFI_INT: {
                int32_t* space = malloc(sizeof(int32_t));
                *space = (int32_t)AS_NUMBER(val);
                ffi_values[i] = space;
                memory_allocations[i] = space;
                break;
            }
            case LUNAR_FFI_DOUBLE: {
                double* space = malloc(sizeof(double));
                *space = AS_NUMBER(val);
                ffi_values[i] = space;
                memory_allocations[i] = space;
                break;
            }
            case LUNAR_FFI_STRING: {
                const char** space = malloc(sizeof(char*));
                *space = AS_CSTRING(val);
                ffi_values[i] = space;
                memory_allocations[i] = space;
                break;
            }
            default:
                ffi_values[i] = NULL;
                memory_allocations[i] = NULL;
                break;
        }
    }

    Value resultVal = NIL_VAL;

    if (func->lunar_return_type == LUNAR_FFI_VOID) {
        ffi_call(&func->cif, FFI_FN(func->func_ptr), NULL, ffi_values);
    } else if (func->lunar_return_type == LUNAR_FFI_INT) {
        int32_t r_int;
        ffi_call(&func->cif, FFI_FN(func->func_ptr), &r_int, ffi_values);
        resultVal = NUMBER_VAL((double)r_int);
    } else if (func->lunar_return_type == LUNAR_FFI_DOUBLE) {
        double r_double;
        ffi_call(&func->cif, FFI_FN(func->func_ptr), &r_double, ffi_values);
        resultVal = NUMBER_VAL(r_double);
    } else if (func->lunar_return_type == LUNAR_FFI_STRING) {
        char* r_str;
        ffi_call(&func->cif, FFI_FN(func->func_ptr), &r_str, ffi_values);
        resultVal = (r_str != NULL) ? OBJ_VAL(copy_str(r_str, (int)strlen(r_str))) : NIL_VAL;
    }

    if (func->arg_count > 0) {
        for (int i = 0; i < func->arg_count; i++) {
            free(memory_allocations[i]);
        }
        free(ffi_values);
        free(memory_allocations);
    }

    return resultVal;
}