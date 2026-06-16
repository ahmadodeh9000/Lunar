#ifndef lunar_compiler_h
#define lunar_compiler_h

#include "chunk.h"
#include "object.h"
#include <ffi/ffi.h>

ObjFunction* compile(const char* src);
void mark_compiler_roots();

#endif
