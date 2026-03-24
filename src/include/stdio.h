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

int printf(const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int snprintf(char *str, size_t size, const char *format, ...);
int sprintf(char *str, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
