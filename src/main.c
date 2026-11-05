#include "vm.h"
#include "lunar_modules.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void repl();
static void run_file(const char* fname);
static char* readFile(const char* path);

int main(int argc, char** argv) {
    init_lunar_vm();

    if (argc == 1) repl();
    else {
        set_script_args(argc - 2, argv + 2);   // the words after the script, for os.args
        run_file(argv[1]);
    }

    free_lunar_vm();
    return 0;
}

static void repl() {
    
    // printing Lunar's author and version
    printf("Lunar %s\nBy Ahmad Odeh\n\n",LUNAR_VERSION);
    char line[1024];
    for (;;) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) { printf("\n"); break; }
        interpret(line, NULL);
    }
}

static void run_file(const char* fname) {
    char* src = readFile(fname);
    InterpretResult ir = interpret(src, fname);
    free(src);
    if (ir == INTERPRET_RUNTIME_ERR) exit(LUNAR_RUNTIME_ERROR_CODE);
    if (ir == INTERPRET_COMPILE_ERR) exit(LUNAR_COMPILETIME_ERROR_CODE);
}

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) { fprintf(stderr, "Could not open \"%s\".\n", path); exit(LUNAR_READ_FILE_FAILURE); }
    fseek(file, 0L, SEEK_END);
    size_t size = ftell(file);
    rewind(file);
    char* buf = malloc(size + 1);
    if (!buf) { fprintf(stderr, "Not enough memory.\n"); exit(LUNAR_NOT_ENOUGH_MEMORY); }
    size_t read = fread(buf, 1, size, file);
    buf[read] = '\0';
    fclose(file);
    return buf;
}
