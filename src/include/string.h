#ifndef STRING_H
#define STRING_H

#include "Std/Types.h"

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *dest, const void *src, int count);
void *memmove(void *dest, const void *src, int count);
void *memset(void *dest, char val, int count);
int memcmp(const void *s1, const void *s2, int n);
int strlen(const char *str);
int strcmp(const char *s1, const char *s2);
char *strcat(char *dest, const char *src);
char *itoa(int n, char *str, int base);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, int n);
int strncmp(const char *s1, const char *s2, int n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

#ifdef __cplusplus
}
#endif

#endif
