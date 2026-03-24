#include <string.h>
#include <stdlib.h>

char *strdup(const char *s) {
    size_t len = strlen(s);
    char *new_s = (char *)malloc(len + 1);
    if (new_s) {
        memcpy(new_s, s, len + 1);
    }
    return new_s;
}
