#include "map.h"
#include "memory.h"

#include <math.h>
#include <string.h>

/*
 * An ordered hash. `entries` keeps every pair in insertion order; `index` is an
 * open-addressing table of positions into `entries`. Removing a pair sets its key
 * to nil (never a valid key) and leaves its index slot pointing at it, so lookups
 * probe past it. Once more than half the entries are removed, they are squeezed
 * out and the index is rebuilt.
 */

#define MAP_MAX_LOAD_NUM 3      // the index is at most 3/4 full
#define MAP_MAX_LOAD_DEN 4

const char* map_key_error(Value key) {
    if (IS_STRING(key) || IS_BOOL(key)) return NULL;
    if (IS_NUMBER(key)) return isnan(AS_NUMBER(key)) ? "Map key can't be NaN." : NULL;
    return "Map key must be a string, number, or bool.";
}

static u32 hash_key(Value key) {
    if (IS_STRING(key)) return AS_STRING(key)->hash;
    if (IS_BOOL(key))   return AS_BOOL(key) ? 1231u : 1237u;

    double d = AS_NUMBER(key);
    if (d == 0) d = 0;                          // -0 and 0 are the same key
    u64 bits;
    memcpy(&bits, &d, sizeof bits);
    bits ^= bits >> 33;                         // mix so nearby numbers spread out
    bits *= 0xff51afd7ed558ccdULL;
    bits ^= bits >> 33;
    return (u32)bits;
}

static void index_insert(ObjMap* map, i32 position) {
    u32 mask = (u32)map->index_capacity - 1;
    u32 slot = hash_key(map->entries[position].key) & mask;
    while (map->index[slot] != -1) slot = (slot + 1) & mask;
    map->index[slot] = position;
}

static void rebuild_index(ObjMap* map) {
    for (i32 i = 0; i < map->index_capacity; i++) map->index[i] = -1;
    for (i32 i = 0; i < map->count; i++) {
        if (!IS_NIL(map->entries[i].key)) index_insert(map, i);
    }
}

// the index slot holding `key`, or -1
static i32 find_slot(ObjMap* map, Value key) {
    if (map->index_capacity == 0) return -1;
    u32 mask = (u32)map->index_capacity - 1;
    u32 slot = hash_key(key) & mask;
    for (;;) {
        i32 position = map->index[slot];
        if (position == -1) return -1;
        MapEntry* entry = &map->entries[position];
        if (!IS_NIL(entry->key) && values_equ(entry->key, key)) return (i32)slot;
        slot = (slot + 1) & mask;
    }
}

// drops removed entries, keeping the order of the rest; allocates nothing
static void compact(ObjMap* map) {
    i32 used = 0;
    for (i32 i = 0; i < map->count; i++) {
        if (!IS_NIL(map->entries[i].key)) map->entries[used++] = map->entries[i];
    }
    map->count = used;
    rebuild_index(map);
}

bool map_get(ObjMap* map, Value key, Value* value) {
    i32 slot = find_slot(map, key);
    if (slot == -1) return false;
    *value = map->entries[map->index[slot]].value;
    return true;
}

void map_set(ObjMap* map, Value key, Value value) {
    i32 slot = find_slot(map, key);
    if (slot != -1) {
        map->entries[map->index[slot]].value = value;   // an update keeps its position
        return;
    }

    // The map stays consistent across each allocation below, since the GC may
    // run inside it and mark the map's entries.
    if ((map->count + 1) * MAP_MAX_LOAD_DEN > map->index_capacity * MAP_MAX_LOAD_NUM) {
        i32 capacity = GROW_CAPACITY(map->index_capacity);
        i32* index   = ALLOCATE(i32, capacity);
        FREE_ARRAY(i32, map->index, map->index_capacity);
        map->index          = index;
        map->index_capacity = capacity;
        rebuild_index(map);
    }
    if (map->count == map->capacity) {
        i32 capacity  = GROW_CAPACITY(map->capacity);
        map->entries  = GROW_ARRAY(MapEntry, map->entries, map->capacity, capacity);
        map->capacity = capacity;
    }

    map->entries[map->count].key   = key;
    map->entries[map->count].value = value;
    index_insert(map, map->count);
    map->count++;
    map->live++;
}

bool map_remove(ObjMap* map, Value key, Value* value) {
    i32 slot = find_slot(map, key);
    if (slot == -1) return false;

    MapEntry* entry = &map->entries[map->index[slot]];
    *value       = entry->value;
    entry->key   = NIL_VAL;
    entry->value = NIL_VAL;
    map->live--;

    if (map->count >= 8 && map->live < map->count / 2) compact(map);
    return true;
}
