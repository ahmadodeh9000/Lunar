// import("string"): functions on strings. Lengths and indexes count bytes;
// chars, char_count, and split with "" walk UTF-8 characters, like for-in does.

#include "lunar_modules.h"
#include "lunar_std.h"
#include "vm.h"
#include "memory.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── argument checks ── */

static bool check_strings(const char* fn, int argc, Value* args, int want) {
    if (!check_arity(fn, argc, want)) return false;
    for (int i = 0; i < want; i++) {
        if (!check_arg(fn, args, i, IS_STRING(args[i]), "a string")) return false;
    }
    return true;
}

static bool check_whole(const char* fn, Value* args, int i) {
    if (!check_arg(fn, args, i, IS_NUMBER(args[i]), "a number")) return false;
    double d = AS_NUMBER(args[i]);
    if (d == floor(d) && fabs(d) < 1e15) return true;
    runtime_error("%s() expects a whole number for argument %d (got %g).", fn, i + 1, d);
    return false;
}

// a byte index that may count from the end, clamped to 0..len
static int clamp_index(double d, int len) {
    if (d < 0) d += len;
    if (d < 0) return 0;
    if (d > len) return len;
    return (int)d;
}

/* ── helpers ── */

// the byte index of sub in s at or after from, or -1
static int find_bytes(ObjString* s, ObjString* sub, int from) {
    if (sub->length == 0) return from <= s->length ? from : -1;
    for (int i = from; i + sub->length <= s->length; i++) {
        const char* p = memchr(s->chars + i, sub->chars[0], s->length - sub->length - i + 1);
        if (p == NULL) return -1;
        i = (int)(p - s->chars);
        if (memcmp(p, sub->chars, sub->length) == 0) return i;
    }
    return -1;
}

static Value new_string(const char* chars, int len) {
    return OBJ_VAL(copy_str(chars, len));
}

// the array of s's UTF-8 characters; a malformed byte is a one-byte string
static Value chars_array(ObjString* s) {
    ObjArray* arr = new_array();
    push(OBJ_VAL(arr));                     // each copy_str can GC
    for (int i = 0; i < s->length; ) {
        int n = utf8_char_length(s->chars + i, s->length - i);
        write_value_array(&arr->items, new_string(s->chars + i, n));
        i += n;
    }
    pop();
    return OBJ_VAL(arr);
}

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/* ── natives ── */

static Value split_native(int argc, Value* args) {
    if (!check_strings("string.split", argc, args, 2)) return NIL_VAL;
    ObjString* s = AS_STRING(args[0]);
    ObjString* sep = AS_STRING(args[1]);
    if (sep->length == 0) return chars_array(s);
    ObjArray* arr = new_array();
    push(OBJ_VAL(arr));
    int start = 0;
    for (;;) {
        int at = find_bytes(s, sep, start);
        int end = at < 0 ? s->length : at;
        write_value_array(&arr->items, new_string(s->chars + start, end - start));
        if (at < 0) break;
        start = at + sep->length;
    }
    pop();
    return OBJ_VAL(arr);
}

static Value join_native(int argc, Value* args) {
    if (!check_arity("string.join", argc, 2)) return NIL_VAL;
    if (!check_arg("string.join", args, 0, IS_ARRAY(args[0]), "an array")) return NIL_VAL;
    if (!check_arg("string.join", args, 1, IS_STRING(args[1]), "a string")) return NIL_VAL;
    ValueArray* items = &AS_ARRAY(args[0])->items;
    ObjString* sep = AS_STRING(args[1]);
    size_t len = 0;
    for (int i = 0; i < items->count; i++) {
        if (!IS_STRING(items->values[i])) {
            runtime_error("string.join() expects an array of strings (got %s at index %d).",
                          value_type_name(items->values[i]), i);
            return NIL_VAL;
        }
        len += AS_STRING(items->values[i])->length + (i > 0 ? sep->length : 0);
    }
    if (len > INT32_MAX - 1) { runtime_error("string.join() result is too long."); return NIL_VAL; }
    char* buf = ALLOCATE(char, len + 1);    // can GC; everything is reachable from args
    char* p = buf;
    for (int i = 0; i < items->count; i++) {
        if (i > 0) { memcpy(p, sep->chars, sep->length); p += sep->length; }
        ObjString* item = AS_STRING(items->values[i]);
        memcpy(p, item->chars, item->length); p += item->length;
    }
    *p = '\0';
    return OBJ_VAL(take_str(buf, (int)len));
}

static Value find_native(int argc, Value* args) {
    if (argc != 2 && argc != 3) {
        runtime_error("string.find() takes 2 or 3 arguments (got %d).", argc);
        return NIL_VAL;
    }
    for (int i = 0; i < 2; i++) {
        if (!check_arg("string.find", args, i, IS_STRING(args[i]), "a string")) return NIL_VAL;
    }
    ObjString* s = AS_STRING(args[0]);
    int start = 0;
    if (argc == 3) {
        if (!check_whole("string.find", args, 2)) return NIL_VAL;
        double d = AS_NUMBER(args[2]);
        if (d > s->length) return NUMBER_VAL(-1);
        start = clamp_index(d, s->length);
    }
    return NUMBER_VAL(find_bytes(s, AS_STRING(args[1]), start));
}

static Value contains_native(int argc, Value* args) {
    if (!check_strings("string.contains", argc, args, 2)) return NIL_VAL;
    return BOOL_VAL(find_bytes(AS_STRING(args[0]), AS_STRING(args[1]), 0) >= 0);
}

static Value starts_with_native(int argc, Value* args) {
    if (!check_strings("string.starts_with", argc, args, 2)) return NIL_VAL;
    ObjString* s = AS_STRING(args[0]);
    ObjString* pre = AS_STRING(args[1]);
    return BOOL_VAL(pre->length <= s->length && memcmp(s->chars, pre->chars, pre->length) == 0);
}

static Value ends_with_native(int argc, Value* args) {
    if (!check_strings("string.ends_with", argc, args, 2)) return NIL_VAL;
    ObjString* s = AS_STRING(args[0]);
    ObjString* suf = AS_STRING(args[1]);
    return BOOL_VAL(suf->length <= s->length &&
                    memcmp(s->chars + s->length - suf->length, suf->chars, suf->length) == 0);
}

static Value replace_native(int argc, Value* args) {
    if (!check_strings("string.replace", argc, args, 3)) return NIL_VAL;
    ObjString* s = AS_STRING(args[0]);
    ObjString* old = AS_STRING(args[1]);
    ObjString* rep = AS_STRING(args[2]);
    if (old->length == 0) { runtime_error("string.replace() can't replace an empty string."); return NIL_VAL; }
    int count = 0;
    for (int at = find_bytes(s, old, 0); at >= 0; at = find_bytes(s, old, at + old->length)) count++;
    if (count == 0) return args[0];
    double len = (double)s->length + (double)count * (rep->length - old->length);
    if (len > INT32_MAX - 1) { runtime_error("string.replace() result is too long."); return NIL_VAL; }
    char* buf = ALLOCATE(char, (size_t)len + 1);
    char* p = buf;
    int from = 0;
    for (int at = find_bytes(s, old, 0); at >= 0; at = find_bytes(s, old, from)) {
        memcpy(p, s->chars + from, at - from); p += at - from;
        memcpy(p, rep->chars, rep->length);    p += rep->length;
        from = at + old->length;
    }
    memcpy(p, s->chars + from, s->length - from); p += s->length - from;
    *p = '\0';
    return OBJ_VAL(take_str(buf, (int)len));
}

static Value slice_native(int argc, Value* args) {
    if (!check_arity("string.slice", argc, 3)) return NIL_VAL;
    if (!check_arg("string.slice", args, 0, IS_STRING(args[0]), "a string")) return NIL_VAL;
    if (!check_whole("string.slice", args, 1) || !check_whole("string.slice", args, 2)) return NIL_VAL;
    ObjString* s = AS_STRING(args[0]);
    int start = clamp_index(AS_NUMBER(args[1]), s->length);
    int end = clamp_index(AS_NUMBER(args[2]), s->length);
    if (end <= start) return new_string("", 0);
    return new_string(s->chars + start, end - start);
}

// upper and lower change ASCII letters only
static Value change_case(const char* fn, int argc, Value* args, bool upper) {
    if (!check_strings(fn, argc, args, 1)) return NIL_VAL;
    ObjString* s = AS_STRING(args[0]);
    char* buf = ALLOCATE(char, s->length + 1);
    for (int i = 0; i < s->length; i++) {
        char c = s->chars[i];
        if (upper && c >= 'a' && c <= 'z') c -= 32;
        else if (!upper && c >= 'A' && c <= 'Z') c += 32;
        buf[i] = c;
    }
    buf[s->length] = '\0';
    return OBJ_VAL(take_str(buf, s->length));
}

static Value upper_native(int argc, Value* args) { return change_case("string.upper", argc, args, true); }
static Value lower_native(int argc, Value* args) { return change_case("string.lower", argc, args, false); }

static Value trim_native(int argc, Value* args) {
    if (!check_strings("string.trim", argc, args, 1)) return NIL_VAL;
    ObjString* s = AS_STRING(args[0]);
    int start = 0, end = s->length;
    while (start < end && is_space(s->chars[start])) start++;
    while (end > start && is_space(s->chars[end - 1])) end--;
    if (start == 0 && end == s->length) return args[0];
    return new_string(s->chars + start, end - start);
}

static Value repeat_native(int argc, Value* args) {
    if (!check_arity("string.repeat", argc, 2)) return NIL_VAL;
    if (!check_arg("string.repeat", args, 0, IS_STRING(args[0]), "a string")) return NIL_VAL;
    if (!check_whole("string.repeat", args, 1)) return NIL_VAL;
    ObjString* s = AS_STRING(args[0]);
    double n = AS_NUMBER(args[1]);
    if (n < 0) { runtime_error("string.repeat() count can't be negative (got %g).", n); return NIL_VAL; }
    if (s->length == 0 || n == 1) return args[0];
    if (s->length * n > INT32_MAX - 1) { runtime_error("string.repeat() result is too long."); return NIL_VAL; }
    int len = s->length * (int)n;
    char* buf = ALLOCATE(char, len + 1);
    for (int i = 0; i < (int)n; i++) memcpy(buf + i * s->length, s->chars, s->length);
    buf[len] = '\0';
    return OBJ_VAL(take_str(buf, len));
}

static Value chars_native(int argc, Value* args) {
    if (!check_strings("string.chars", argc, args, 1)) return NIL_VAL;
    return chars_array(AS_STRING(args[0]));
}

static Value char_count_native(int argc, Value* args) {
    if (!check_strings("string.char_count", argc, args, 1)) return NIL_VAL;
    ObjString* s = AS_STRING(args[0]);
    int count = 0;
    for (int i = 0; i < s->length; count++) i += utf8_char_length(s->chars + i, s->length - i);
    return NUMBER_VAL(count);
}

// a decimal number, with optional sign, fraction, and exponent, and ASCII
// whitespace around it; nothing else (no hex, inf, or nan)
static Value to_number_native(int argc, Value* args) {
    if (!check_strings("string.to_number", argc, args, 1)) return NIL_VAL;
    ObjString* s = AS_STRING(args[0]);
    const char* p = s->chars;
    const char* end = s->chars + s->length;
    while (p < end && is_space(*p)) p++;
    while (end > p && is_space(end[-1])) end--;
    const char* q = p;
    if (q < end && (*q == '+' || *q == '-')) q++;
    int digits = 0;
    while (q < end && *q >= '0' && *q <= '9') { q++; digits++; }
    if (q < end && *q == '.') {
        q++;
        while (q < end && *q >= '0' && *q <= '9') { q++; digits++; }
    }
    if (digits > 0 && q < end && (*q == 'e' || *q == 'E')) {
        q++;
        if (q < end && (*q == '+' || *q == '-')) q++;
        int exp_digits = 0;
        while (q < end && *q >= '0' && *q <= '9') { q++; exp_digits++; }
        if (exp_digits == 0) digits = 0;
    }
    if (digits == 0 || q != end) {
        runtime_error("string.to_number() can't convert \"%s\" to a number.", s->chars);
        return NIL_VAL;
    }
    return NUMBER_VAL(strtod(p, NULL));     // s->chars is NUL-terminated; trailing spaces stop it
}

static Value byte_native(int argc, Value* args) {
    if (!check_arity("string.byte", argc, 2)) return NIL_VAL;
    if (!check_arg("string.byte", args, 0, IS_STRING(args[0]), "a string")) return NIL_VAL;
    if (!check_whole("string.byte", args, 1)) return NIL_VAL;
    ObjString* s = AS_STRING(args[0]);
    double i = AS_NUMBER(args[1]);
    if (i < 0 || i >= s->length) {
        runtime_error("string.byte() index %g out of bounds (length %d).", i, s->length);
        return NIL_VAL;
    }
    return NUMBER_VAL((u8)s->chars[(int)i]);
}

static Value from_byte_native(int argc, Value* args) {
    if (!check_arity("string.from_byte", argc, 1)) return NIL_VAL;
    if (!check_whole("string.from_byte", args, 0)) return NIL_VAL;
    double n = AS_NUMBER(args[0]);
    if (n < 0 || n > 255) {
        runtime_error("string.from_byte() expects a byte from 0 to 255 (got %g).", n);
        return NIL_VAL;
    }
    char c = (char)(u8)n;
    return new_string(&c, 1);
}

void open_string_module(ObjMap* map) {
    define_module_native(map, "split",       split_native);
    define_module_native(map, "join",        join_native);
    define_module_native(map, "find",        find_native);
    define_module_native(map, "contains",    contains_native);
    define_module_native(map, "starts_with", starts_with_native);
    define_module_native(map, "ends_with",   ends_with_native);
    define_module_native(map, "replace",     replace_native);
    define_module_native(map, "slice",       slice_native);
    define_module_native(map, "upper",       upper_native);
    define_module_native(map, "lower",       lower_native);
    define_module_native(map, "trim",        trim_native);
    define_module_native(map, "repeat",      repeat_native);
    define_module_native(map, "chars",       chars_native);
    define_module_native(map, "char_count",  char_count_native);
    define_module_native(map, "to_number",   to_number_native);
    define_module_native(map, "byte",        byte_native);
    define_module_native(map, "from_byte",   from_byte_native);
}
