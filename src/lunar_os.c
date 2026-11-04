// import("os"): the process and its surroundings: arguments, environment,
// commands, time, and standard input. A failure raises an error with the OS reason.

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L     // posix_spawn, getline, clock_gettime on Linux
#endif

#include "lunar_modules.h"
#include "lunar_std.h"
#include "vm.h"
#include "map.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

static int    script_argc = 0;
static char** script_argv = NULL;

void set_script_args(int argc, char** argv) {
    script_argc = argc;
    script_argv = argv;
}

/* ── helpers ── */

// argument i is a string that C can use, with no NUL byte inside
static bool check_c_string(const char* fn, Value* args, int i, const char* what) {
    if (!check_arg(fn, args, i, IS_STRING(args[i]), "a string")) return false;
    ObjString* s = AS_STRING(args[i]);
    if (memchr(s->chars, '\0', s->length) != NULL) {
        runtime_error("%s() %s can't contain a NUL byte.", fn, what);
        return false;
    }
    return true;
}

static double clock_seconds(clockid_t id) {
    struct timespec ts;
    clock_gettime(id, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct { char* buf; size_t len; size_t cap; } Bytes;

static void bytes_append(Bytes* b, const char* s, size_t n) {
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->len + n) cap *= 2;
        b->buf = (char*)realloc(b->buf, cap);
        if (b->buf == NULL) { fprintf(stderr, "OOM os\n"); exit(1); }
        b->cap = cap;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
}

// sets map[name] to a string made from these bytes; the map must be reachable
static void set_string_field(ObjMap* map, const char* name, const char* chars, int len) {
    push(OBJ_VAL(copy_str(chars, len)));
    push(OBJ_VAL(copy_str(name, (int)strlen(name))));
    map_set(map, lvm.stack_top[-1], lvm.stack_top[-2]);
    pop(); pop();
}

/* ── natives ── */

static Value env_native(int argc, Value* args) {
    if (!check_arity("os.env", argc, 1) || !check_c_string("os.env", args, 0, "name")) return NIL_VAL;
    const char* value = getenv(AS_STRING(args[0])->chars);
    if (value == NULL) return NIL_VAL;
    return OBJ_VAL(copy_str(value, (int)strlen(value)));
}

static Value exit_native(int argc, Value* args) {
    if (argc > 1) { runtime_error("os.exit() takes 0 or 1 arguments (got %d).", argc); return NIL_VAL; }
    int code = 0;
    if (argc == 1) {
        if (!check_arg("os.exit", args, 0, IS_NUMBER(args[0]), "a number")) return NIL_VAL;
        double d = AS_NUMBER(args[0]);
        if (!(d >= 0 && d <= 255 && d == floor(d))) {
            runtime_error("os.exit() expects a code from 0 to 255 (got %g).", d);
            return NIL_VAL;
        }
        code = (int)d;
    }
    fflush(stdout);
    fflush(stderr);
    exit(code);
}

// runs cmd with /bin/sh -c and returns { code, out, err }; a non-zero exit is just
// the code, and only a command that can't be started raises an error
static Value run_native(int argc, Value* args) {
    if (!check_arity("os.run", argc, 1) || !check_c_string("os.run", args, 0, "command")) return NIL_VAL;
    ObjString* cmd = AS_STRING(args[0]);

    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0) {
        runtime_error("os.run() can't run \"%s\": %s.", cmd->chars, strerror(errno));
        return NIL_VAL;
    }
    if (pipe(err_pipe) != 0) {
        int err = errno;
        close(out_pipe[0]); close(out_pipe[1]);
        runtime_error("os.run() can't run \"%s\": %s.", cmd->chars, strerror(err));
        return NIL_VAL;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], 1);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], 2);
    posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[1]);
    char* child_argv[] = { "sh", "-c", cmd->chars, NULL };
    pid_t pid;
    int spawn_err = posix_spawn(&pid, "/bin/sh", &actions, NULL, child_argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(out_pipe[1]);
    close(err_pipe[1]);
    if (spawn_err != 0) {
        close(out_pipe[0]); close(err_pipe[0]);
        runtime_error("os.run() can't run \"%s\": %s.", cmd->chars, strerror(spawn_err));
        return NIL_VAL;
    }

    // read both pipes as data comes, so a child filling either one can't block
    Bytes out = {0}, err = {0};
    struct pollfd fds[2] = { { out_pipe[0], POLLIN, 0 }, { err_pipe[0], POLLIN, 0 } };
    Bytes* sinks[2] = { &out, &err };
    int open_count = 2;
    char chunk[65536];
    while (open_count > 0) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < 2; i++) {
            if (fds[i].fd < 0 || fds[i].revents == 0) continue;
            ssize_t n = read(fds[i].fd, chunk, sizeof chunk);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { close(fds[i].fd); fds[i].fd = -1; open_count--; continue; }
            bytes_append(sinks[i], chunk, (size_t)n);
        }
    }
    for (int i = 0; i < 2; i++) if (fds[i].fd >= 0) close(fds[i].fd);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    int code = WIFEXITED(status) ? WEXITSTATUS(status)
             : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;

    if (out.len > INT32_MAX - 1 || err.len > INT32_MAX - 1) {
        free(out.buf); free(err.buf);
        runtime_error("os.run() output of \"%s\" is too large.", cmd->chars);
        return NIL_VAL;
    }
    ObjMap* result = new_map();
    push(OBJ_VAL(result));
    define_module_value(result, "code", NUMBER_VAL(code));
    set_string_field(result, "out", out.len ? out.buf : "", (int)out.len);
    set_string_field(result, "err", err.len ? err.buf : "", (int)err.len);
    pop();
    free(out.buf);
    free(err.buf);
    return OBJ_VAL(result);
}

static Value time_native(int argc, Value* args) {
    if (!check_arity("os.time", argc, 0)) return NIL_VAL;
    return NUMBER_VAL(clock_seconds(CLOCK_REALTIME));
}

// wall-clock seconds from a clock that never jumps, for measuring elapsed time
static Value clock_native(int argc, Value* args) {
    if (!check_arity("os.clock", argc, 0)) return NIL_VAL;
    return NUMBER_VAL(clock_seconds(CLOCK_MONOTONIC));
}

static Value sleep_native(int argc, Value* args) {
    if (!check_arity("os.sleep", argc, 1)) return NIL_VAL;
    if (!check_arg("os.sleep", args, 0, IS_NUMBER(args[0]), "a number")) return NIL_VAL;
    double d = AS_NUMBER(args[0]);
    if (d < 0) { runtime_error("os.sleep() can't sleep a negative time (got %g).", d); return NIL_VAL; }
    if (!(d <= 1e9)) { runtime_error("os.sleep() can't sleep that long (got %g).", d); return NIL_VAL; }
    struct timespec ts = { (time_t)d, (long)((d - floor(d)) * 1e9) };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
    return NIL_VAL;
}

static Value cwd_native(int argc, Value* args) {
    if (!check_arity("os.cwd", argc, 0)) return NIL_VAL;
    char* dir = getcwd(NULL, 0);
    if (dir == NULL) {
        runtime_error("os.cwd() can't get the current directory: %s.", strerror(errno));
        return NIL_VAL;
    }
    ObjString* s = copy_str(dir, (int)strlen(dir));
    free(dir);
    return OBJ_VAL(s);
}

// one line from stdin without its "\n" or "\r\n", or nil at the end of input
static Value input_native(int argc, Value* args) {
    if (argc > 1) { runtime_error("os.input() takes 0 or 1 arguments (got %d).", argc); return NIL_VAL; }
    if (argc == 1) {
        if (!check_arg("os.input", args, 0, IS_STRING(args[0]), "a string")) return NIL_VAL;
        fwrite(AS_CSTRING(args[0]), 1, AS_STRING(args[0])->length, stdout);
    }
    fflush(stdout);
    char* line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, stdin);
    if (n < 0) {
        int err = errno;
        bool failed = ferror(stdin);
        free(line);
        clearerr(stdin);
        if (failed) runtime_error("os.input() can't read input: %s.", strerror(err));
        return NIL_VAL;
    }
    if (n > 0 && line[n - 1] == '\n') n--;
    if (n > 0 && line[n - 1] == '\r') n--;
    if (n > INT32_MAX - 1) { free(line); runtime_error("os.input() line is too long."); return NIL_VAL; }
    ObjString* s = copy_str(line, (int)n);
    free(line);
    return OBJ_VAL(s);
}

static const char* platform_name() {
#if defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#elif defined(_WIN32)
    return "windows";
#else
    return "other";
#endif
}

void open_os_module(ObjMap* map) {
    ObjArray* argv = new_array();
    push(OBJ_VAL(argv));                    // each copy_str can GC
    for (int i = 0; i < script_argc; i++) {
        write_value_array(&argv->items, OBJ_VAL(copy_str(script_argv[i], (int)strlen(script_argv[i]))));
    }
    define_module_value(map, "args", OBJ_VAL(argv));
    pop();
    const char* platform = platform_name();
    push(OBJ_VAL(copy_str(platform, (int)strlen(platform))));
    define_module_value(map, "platform", lvm.stack_top[-1]);
    pop();

    define_module_native(map, "env",   env_native);
    define_module_native(map, "exit",  exit_native);
    define_module_native(map, "run",   run_native);
    define_module_native(map, "time",  time_native);
    define_module_native(map, "clock", clock_native);
    define_module_native(map, "sleep", sleep_native);
    define_module_native(map, "cwd",   cwd_native);
    define_module_native(map, "input", input_native);
}
