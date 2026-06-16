// on god, i don't know why it works lol, nah jk 


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

#ifdef __APPLE__
#include <ffi/ffi.h>
#else
#include <ffi.h>
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
   Usage in Script: let lib = ffiLoad("libc.so.6");
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

    ObjFFILib* lib = new_ffi_lib(path, handle);
    return OBJ_VAL(lib);
}

/* ─────────────────────────────────────────────────────────────
   Native Function: ffi_bind_function (Variadic Implementation)
   Usage in Script: let puts = ffiBind(lib, "puts", "i32", "string");
   * Claude is good for writting comments lol
   ───────────────────────────────────────────────────────────── */
Value ffi_bind_function(int argCount, Value* args) {
    // Requires minimum 3 arguments: library object, function name string, return type string
    if (argCount < 3 || !IS_FFI_LIB(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2])) {
        return NIL_VAL;
    }

    ObjFFILib* lib = AS_FFI_LIB(args[0]);
    ObjString* funcName = AS_STRING(args[1]);
    ObjString* returnTypeStr = AS_STRING(args[2]);
    
    // Everything following the return type is an explicit parameter string
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

    ObjFFIFunc* func = new_ffi_func(funcName, symbol);
    func->arg_count = param_count;
    
    // Allocate type descriptor tracking slots using VM allocation macros
    func->arg_types = ALLOCATE(ffi_type*, param_count);
    func->lunar_arg_types = ALLOCATE(int, param_count);

    // Parse the return type layout
    if (!parse_type(returnTypeStr, &func->rtype, &func->lunar_return_type)) {
        // If parsing fails, cleanly free dynamically allocated arrays inside the half-formed object
        FREE_ARRAY(ffi_type*, func->arg_types, param_count);
        FREE_ARRAY(int, func->lunar_arg_types, param_count);
        return NIL_VAL;
    }

    // Loop through the trailing parameters passed directly to this call frame
    for (int i = 0; i < param_count; i++) {
        Value typeVal = args[3 + i];
        if (!IS_STRING(typeVal) || !parse_type(AS_STRING(typeVal), &func->arg_types[i], &func->lunar_arg_types[i])) {
            FREE_ARRAY(ffi_type*, func->arg_types, param_count);
            FREE_ARRAY(int, func->lunar_arg_types, param_count);
            return NIL_VAL;
        }
    }

    // Prepare libffi Call Interface (CIF) layout definition metadata
    if (ffi_prep_cif(&func->cif, FFI_DEFAULT_ABI, func->arg_count, func->rtype, func->arg_types) != FFI_OK) {
        FREE_ARRAY(ffi_type*, func->arg_types, param_count);
        FREE_ARRAY(int, func->lunar_arg_types, param_count);
        return NIL_VAL;
    }

    return OBJ_VAL(func);
}

/* ─────────────────────────────────────────────────────────────
   Internal Worker Execution: ffi_call_bound
   Invoked directly via the VM execution loop when hit by OP_CALL
   ───────────────────────────────────────────────────────────── */
Value ffi_call_bound(ObjFFIFunc* func, int argCount, Value* args) {
    if (argCount != func->arg_count) {
        fprintf(stderr, "FFI Runtime Error: Function '%s' expected %d arguments, got %d.\n", 
                func->name->chars, func->arg_count, argCount);
        return NIL_VAL;
    }

    // Allocate storage for parameter address bindings
    void** ffi_values = NULL;
    void** memory_allocations = NULL;

    if (func->arg_count > 0) {
        ffi_values = malloc(sizeof(void*) * func->arg_count);
        memory_allocations = malloc(sizeof(void*) * func->arg_count);
    }

    // Map each Lunar value sitting on the stack into local C variables
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
        resultVal = NIL_VAL;
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
        if (r_str != NULL) {
            resultVal = OBJ_VAL(copy_str(r_str, (int)strlen(r_str)));
        } else {
            resultVal = NIL_VAL;
        }
    }

    // Cleanup ephemeral value-packing space mappings
    if (func->arg_count > 0) {
        for (int i = 0; i < func->arg_count; i++) {
            if (memory_allocations[i] != NULL) {
                free(memory_allocations[i]);
            }
        }
        free(ffi_values);
        free(memory_allocations);
    }

    return resultVal;
}