#ifndef lunar_modules_h
#define lunar_modules_h

#include "object.h"

// import("name"): the name is on top of the stack, and is replaced by the module's
// map. Kept out of vm.c so it isn't inlined into the dispatch loop.
bool import_builtin(ObjString* name);

// Built-in modules: each fills the map that import("name") returns.
void open_string_module(ObjMap* map);
void open_fs_module(ObjMap* map);
void open_os_module(ObjMap* map);

// adds a value (not a function) to a built-in module's map
void define_module_value(ObjMap* map, const char* name, Value value);

// the command-line words after the script's name, for os.args (main.c sets them)
void set_script_args(int argc, char** argv);

#endif
