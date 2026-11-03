#ifndef lunar_compiler_h
#define lunar_compiler_h

#include "chunk.h"
#include "object.h"

#if __has_include(<ffi.h>)
#include <ffi.h>
#elif __has_include(<ffi/ffi.h>)
#include <ffi/ffi.h>
#else
#error "libffi header not found"
#endif

// compiles a file's source; every function it makes belongs to module
ObjFunction* compile(const char* src, ObjModule* module);
void mark_compiler_roots();

#endif
