#ifndef lunar_ffi_h
#define lunar_ffi_h

#include "value.h"
#include "object.h"

typedef enum {
    LUNAR_FFI_INT,
    LUNAR_FFI_DOUBLE,
    LUNAR_FFI_STRING,
    LUNAR_FFI_VOID
} LunarFFIType;

Value  ffi_load_library(int argCount, Value* args);
Value  ffi_bind_function(int argCount, Value* args);
Value ffi_call_bound(ObjFFIFunc* func, int argCount, Value* args);

#endif