// import("fs"): files and directories. Relative paths start from the current
// directory (not the script's, unlike import). Files are read and written as raw
// bytes. A failure raises an error with the OS reason.

#include "lunar_modules.h"
#include "lunar_std.h"
#include "vm.h"
#include "memory.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ── argument checks and errors ── */

// argument i is a string that C can use as a path
static bool check_path(const char* fn, Value* args, int i) {
    if (!check_arg(fn, args, i, IS_STRING(args[i]), "a string")) return false;
    ObjString* path = AS_STRING(args[i]);
    if (memchr(path->chars, '\0', path->length) != NULL) {
        runtime_error("%s() path can't contain a NUL byte.", fn);
        return false;
    }
    return true;
}

// e.g. fs.read() can't read "notes.txt": No such file or directory.
static void fail(const char* fn, const char* verb, ObjString* path, int err) {
    runtime_error("%s() can't %s \"%s\": %s.", fn, verb, path->chars, strerror(err));
}

/* ── helpers ── */

// the whole file in a malloc'd buffer, or NULL after raising an error
static char* read_all(const char* fn, ObjString* path, int* length) {
    struct stat st;
    if (stat(path->chars, &st) != 0) { fail(fn, "read", path, errno); return NULL; }
    if (S_ISDIR(st.st_mode)) { fail(fn, "read", path, EISDIR); return NULL; }
    FILE* f = fopen(path->chars, "rb");
    if (f == NULL) { fail(fn, "read", path, errno); return NULL; }
    // read in chunks: some files (pipes, /proc) don't know their size
    size_t cap = st.st_size > 0 ? (size_t)st.st_size + 1 : 4096, len = 0;
    char* buf = NULL;
    for (;;) {
        if (cap > (size_t)INT32_MAX) {
            free(buf); fclose(f);
            runtime_error("%s() can't read \"%s\": file is too large.", fn, path->chars);
            return NULL;
        }
        char* grown = (char*)realloc(buf, cap);
        if (grown == NULL) { fprintf(stderr, "OOM fs\n"); exit(1); }
        buf = grown;
        len += fread(buf + len, 1, cap - len, f);
        if (len < cap) break;
        cap *= 2;
    }
    if (ferror(f)) {
        int err = errno;
        free(buf); fclose(f);
        fail(fn, "read", path, err);
        return NULL;
    }
    fclose(f);
    *length = (int)len;
    return buf;
}

static Value write_file(const char* fn, int argc, Value* args, const char* mode) {
    if (!check_arity(fn, argc, 2) || !check_path(fn, args, 0)) return NIL_VAL;
    if (!check_arg(fn, args, 1, IS_STRING(args[1]), "a string")) return NIL_VAL;
    ObjString* path = AS_STRING(args[0]);
    ObjString* text = AS_STRING(args[1]);
    FILE* f = fopen(path->chars, mode);
    if (f == NULL) { fail(fn, "write", path, errno); return NIL_VAL; }
    size_t n = fwrite(text->chars, 1, text->length, f);
    int err = n < (size_t)text->length ? errno : 0;
    if (fclose(f) != 0 && err == 0) err = errno;
    if (err != 0) fail(fn, "write", path, err);
    return NIL_VAL;
}

static int compare_names(const void* a, const void* b) {
    return strcmp(*(char* const*)a, *(char* const*)b);
}

/* ── natives ── */

static Value read_native(int argc, Value* args) {
    if (!check_arity("fs.read", argc, 1) || !check_path("fs.read", args, 0)) return NIL_VAL;
    int len;
    char* buf = read_all("fs.read", AS_STRING(args[0]), &len);
    if (buf == NULL) return NIL_VAL;
    ObjString* s = copy_str(buf, len);
    free(buf);
    return OBJ_VAL(s);
}

static Value write_native(int argc, Value* args)  { return write_file("fs.write", argc, args, "wb"); }
static Value append_native(int argc, Value* args) { return write_file("fs.append", argc, args, "ab"); }

static Value exists_native(int argc, Value* args) {
    if (!check_arity("fs.exists", argc, 1) || !check_path("fs.exists", args, 0)) return NIL_VAL;
    struct stat st;
    return BOOL_VAL(stat(AS_STRING(args[0])->chars, &st) == 0);
}

static Value is_dir_native(int argc, Value* args) {
    if (!check_arity("fs.is_dir", argc, 1) || !check_path("fs.is_dir", args, 0)) return NIL_VAL;
    struct stat st;
    return BOOL_VAL(stat(AS_STRING(args[0])->chars, &st) == 0 && S_ISDIR(st.st_mode));
}

static Value list_native(int argc, Value* args) {
    if (!check_arity("fs.list", argc, 1) || !check_path("fs.list", args, 0)) return NIL_VAL;
    ObjString* path = AS_STRING(args[0]);
    DIR* dir = opendir(path->chars);
    if (dir == NULL) { fail("fs.list", "list", path, errno); return NIL_VAL; }
    char** names = NULL;
    int count = 0, cap = 0;
    for (;;) {
        errno = 0;
        struct dirent* entry = readdir(dir);
        if (entry == NULL) break;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 16;
            names = (char**)realloc(names, sizeof(char*) * cap);
            if (names == NULL) { fprintf(stderr, "OOM fs\n"); exit(1); }
        }
        names[count] = strdup(entry->d_name);
        if (names[count] == NULL) { fprintf(stderr, "OOM fs\n"); exit(1); }
        count++;
    }
    int err = errno;
    closedir(dir);
    if (err != 0) {
        for (int i = 0; i < count; i++) free(names[i]);
        free(names);
        fail("fs.list", "list", path, err);
        return NIL_VAL;
    }
    if (count > 0) qsort(names, count, sizeof(char*), compare_names);
    ObjArray* arr = new_array();
    push(OBJ_VAL(arr));                     // each copy_str can GC
    for (int i = 0; i < count; i++) {
        write_value_array(&arr->items, OBJ_VAL(copy_str(names[i], (int)strlen(names[i]))));
        free(names[i]);
    }
    free(names);
    pop();
    return OBJ_VAL(arr);
}

// a file, or an empty directory; a symlink is removed, not what it points to
static Value remove_native(int argc, Value* args) {
    if (!check_arity("fs.remove", argc, 1) || !check_path("fs.remove", args, 0)) return NIL_VAL;
    ObjString* path = AS_STRING(args[0]);
    if (remove(path->chars) != 0) fail("fs.remove", "remove", path, errno);
    return NIL_VAL;
}

// makes path and any missing parents; a directory that's already there is fine
static Value mkdir_native(int argc, Value* args) {
    if (!check_arity("fs.mkdir", argc, 1) || !check_path("fs.mkdir", args, 0)) return NIL_VAL;
    ObjString* path = AS_STRING(args[0]);
    char* buf = (char*)malloc(path->length + 1);
    if (buf == NULL) { fprintf(stderr, "OOM fs\n"); exit(1); }
    memcpy(buf, path->chars, path->length + 1);
    int len = path->length;
    while (len > 1 && buf[len - 1] == '/') buf[--len] = '\0';
    for (int i = 1; i <= len; i++) {
        if (i < len && buf[i] != '/') continue;
        if (i < len && buf[i - 1] == '/') continue;     // "a//b"
        char saved = buf[i];
        buf[i] = '\0';
        if (mkdir(buf, 0777) != 0) {
            int err = errno;
            struct stat st;
            if (err != EEXIST || stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
                if (err == EEXIST && i < len) err = ENOTDIR;    // a parent is a file
                free(buf);
                fail("fs.mkdir", "create", path, err);
                return NIL_VAL;
            }
        }
        buf[i] = saved;
    }
    free(buf);
    if (len == 0) fail("fs.mkdir", "create", path, ENOENT);
    return NIL_VAL;
}

// the file's lines, without their "\n" or "\r\n"
static Value lines_native(int argc, Value* args) {
    if (!check_arity("fs.lines", argc, 1) || !check_path("fs.lines", args, 0)) return NIL_VAL;
    int len;
    char* buf = read_all("fs.lines", AS_STRING(args[0]), &len);
    if (buf == NULL) return NIL_VAL;
    ObjArray* arr = new_array();
    push(OBJ_VAL(arr));
    int start = 0;
    while (start < len) {
        char* nl = memchr(buf + start, '\n', len - start);
        int end = nl ? (int)(nl - buf) : len;
        int stop = nl && end > start && buf[end - 1] == '\r' ? end - 1 : end;
        write_value_array(&arr->items, OBJ_VAL(copy_str(buf + start, stop - start)));
        start = end + 1;
    }
    pop();
    free(buf);
    return OBJ_VAL(arr);
}

void open_fs_module(ObjMap* map) {
    define_module_native(map, "read",   read_native);
    define_module_native(map, "write",  write_native);
    define_module_native(map, "append", append_native);
    define_module_native(map, "exists", exists_native);
    define_module_native(map, "is_dir", is_dir_native);
    define_module_native(map, "list",   list_native);
    define_module_native(map, "remove", remove_native);
    define_module_native(map, "mkdir",  mkdir_native);
    define_module_native(map, "lines",  lines_native);
}
