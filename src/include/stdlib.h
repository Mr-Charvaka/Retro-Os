#ifndef STDLIB_H
#define STDLIB_H

#include "Std/Types.h"

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void free(void *p);
void *calloc(size_t nmemb, size_t size);

inline void exit(int status) {
  while (1)
    ;
}
inline int abs(int j) { return j < 0 ? -j : j; }

#ifdef __cplusplus
}
#endif

#endif
