#include <stdio.h>

// We use attribute((visibility("default"))) to ensure macOS exports the symbol 
__attribute__((visibility("default")))
void greet_ahmad(const char* greeting) {
    printf("%s, Ahmad!\n", greeting);
}

__attribute__((visibility("default")))
int add_numbers(int a, int b) {
    return a + b;
}