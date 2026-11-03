#ifndef lunar_map_h
#define lunar_map_h

#include "common.h"
#include "value.h"
#include "object.h"

// NULL if the key is valid, otherwise the runtime error message
const char* map_key_error(Value key);

// Keys must pass map_key_error. map_set can allocate, so it can trigger a GC:
// the map, key, and value must be reachable (e.g. on the VM stack) while it runs.
bool map_get(ObjMap* map, Value key, Value* value);
void map_set(ObjMap* map, Value key, Value value);
bool map_remove(ObjMap* map, Value key, Value* value);

#endif
