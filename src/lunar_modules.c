// Built-in modules: import("name") opens each one once, and every importer gets
// the same map.

#include "lunar_modules.h"
#include "lunar_std.h"
#include "vm.h"
#include "map.h"

#include <string.h>

// adds a native to a built-in module's map, which the caller keeps reachable
void define_module_native(ObjMap* map, const char* name, NativeFn fn) {
    push(OBJ_VAL(copy_str(name,(int)strlen(name))));
    push(OBJ_VAL(new_native(fn,name)));
    map_set(map, lvm.stack_top[-2], lvm.stack_top[-1]);
    pop(); pop();
}

// adds a value to a built-in module's map; the caller keeps the map and value reachable
void define_module_value(ObjMap* map, const char* name, Value value) {
    push(value);
    push(OBJ_VAL(copy_str(name,(int)strlen(name))));
    map_set(map, lvm.stack_top[-1], lvm.stack_top[-2]);
    pop(); pop();
}

// the modules import("name") opens, each once; their maps are shared by every importer
static const struct { const char* name; void (*open)(ObjMap*); } builtin_modules[] = {
    { "string", open_string_module },
    { "fs",     open_fs_module },
    { "os",     open_os_module },
};

// import("name"): the name is on top of the stack, and is replaced by the module's map
bool import_builtin(ObjString* name) {
    Value cached;
    if(table_get(&lvm.builtin_modules,name,&cached)){
        lvm.stack_top[-1]=cached;
        return true;
    }
    for(size_t i=0;i<sizeof builtin_modules/sizeof builtin_modules[0];i++){
        if(strlen(builtin_modules[i].name)!=(size_t)name->length ||
           memcmp(builtin_modules[i].name,name->chars,name->length)!=0) continue;
        ObjMap* map=new_map();
        push(OBJ_VAL(map));                             // the name stays below it
        builtin_modules[i].open(map);
        table_set(&lvm.builtin_modules,name,OBJ_VAL(map));
        pop();
        lvm.stack_top[-1]=OBJ_VAL(map);
        return true;
    }
    runtime_error("No built-in module named '%s' (to import a file, use '%s.lunar').",name->chars,name->chars);
    return false;
}
