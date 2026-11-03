#ifndef lunar_std_h__
#define lunar_std_h__


#include "value.h"

void register_std_natives();

// the text print shows for a value, in a malloc'd buffer the caller frees
char* value_text(Value v, int* length);

// Argument checks for natives. On a mismatch they raise a runtime error and return
// false, and the native must then return at once without touching anything else.
const char* value_type_name(Value v);
bool check_arity(const char* fn, int argc, int want);
bool check_arg(const char* fn, Value* args, int i, bool ok, const char* want);
bool check_numbers(const char* fn, int argc, Value* args, int want);


#endif