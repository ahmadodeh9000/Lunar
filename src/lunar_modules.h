#ifndef lunar_modules_h
#define lunar_modules_h

#include "object.h"

// import("name"): the name is on top of the stack, and is replaced by the module's
// map. Kept out of vm.c so it isn't inlined into the dispatch loop.
bool import_builtin(ObjString* name);

// Built-in modules: each fills the map that import("name") returns.
void open_string_module(ObjMap* map);

#endif
