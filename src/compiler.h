#ifndef lunar_compiler_h
#define lunar_compiler_h

#include "chunk.h"
#include "object.h"

#ifdef __APPLE__
#include <ffi/ffi.h>
#else
#include <ffi.h>
#endif

ObjFunction* compile(const char* src);
void mark_compiler_roots();

#endif
