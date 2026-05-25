#include "compiler.h"
#include "scanner.h"
#include "common.h"

#include <stdio.h>

void compiler(const char* src) {
    init_scanner(src);

    i32 line = -1;

    for (;;) {

        Token tok = scan_token();

        if (line != tok.line) {
            printf("%4d",tok.line);
        }
        else {
            printf("    |");
        }

        printf("%2d '%.*s'\n",tok.line, tok.length,tok.start);

        if (TOKEN_EOF == tok.type) break;


    }
    
}
