#ifndef STDIO_H
#define STDIO_H

#include "string.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FILE void
#define stdout ((void *)1)
#define stderr ((void *)2)

inline int printf(const char *format, ...) { return 0; }
inline int snprintf(char *str, size_t size, const char *format, ...) {
  if (size > 0)
    str[0] = 0;
  return 0;
}
inline int sprintf(char *str, const char *format, ...) {
  str[0] = 0;
  return 0;
}

#ifdef __cplusplus
}
#endif

#endif
