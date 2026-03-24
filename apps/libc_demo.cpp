#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void _start() {
    printf("LibC Demo Application Initialized\n");
    printf("==================================\n");

    // Test malloc
    char *ptr = (char *)malloc(100);
    if (ptr) {
        strcpy(ptr, "Memory allocation using Newlib's malloc working!\n");
        printf("%s", ptr);
        free(ptr);
    } else {
        printf("Malloc failed!\n");
    }

    // Test printf with various formats
    int x = 42;
    float f = 3.14159;
    printf("Integer: %d, Hex: 0x%X\n", x, x);
    // Note: floating point might need extra setup in some Newlib builds, but let's try.
    printf("Float: %.2f\n", (double)f);

    printf("\nLibC Demo execution completed.\n");
    
    // Exit using Newlib's exit which calls our _exit stub
    exit(0);
}
