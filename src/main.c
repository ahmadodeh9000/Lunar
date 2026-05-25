
#include "vm.h"
#include "debug.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void repl();
static void run_file(const char* fname);
static char* readFile(const char* path);

int main(int argc, char** argv) {

	init_lunar_vm();
	
	if (argc == 1) {
		repl();
	}

	else if (argc == 2) {
		run_file(argv[1]);
	}

	else {
		fprintf(stderr, "Usage: lunar [path]");
	}


	free_lunar_vm();
	
	return 0;

}



static void repl() {

	char line[1024];

	for (;;) {
		printf("> ");

		if (!fgets(line,1024,stdin)) {
			printf("\n");
			break;
		}

		interpret(line);
	}

}

static void run_file(const char* fname) {

	const char* src 	= readFile(fname);
	InterpretResult ir 	= interpret(src);

	if (INTERPRET_RUNTIME_ERR == ir) 	exit(LUNAR_RUNTIME_ERROR_CODE);
	if (INTERPRET_COMPPILE_ERR == ir)	exit(LUNAR_COMPILETIME_ERROR_CODE);

}


static char* readFile(const char* path) {
  	FILE* file = fopen(path, "rb");

  	if (NULL == file) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		exit(LUNAR_READ_FILE_FAILURE);
  	}

  	fseek(file, 0L, SEEK_END);
  	size_t fileSize = ftell(file);
  	rewind(file);

  	char* buffer = (char*)malloc(fileSize + 1);

	if (NULL == buffer) {
		fprintf(stderr,"NOT ENOUGH MEMORY TO READ !\n");
		exit(LUNAR_NOT_ENOUGH_MEMORY);
	}

  	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

	if (bytesRead < fileSize) {
    	fprintf(stderr, "Could not read file \"%s\".\n", path);
    	exit(-1);
  	}

  	buffer[bytesRead] = '\0';

  	fclose(file);
  	return buffer;
}


