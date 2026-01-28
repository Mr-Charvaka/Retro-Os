#ifndef CTYPE_H
#define CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

static inline int isalpha(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static inline int isdigit(int c) { return (c >= '0' && c <= '9'); }
static inline int isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int isspace(int c) {
  return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
          c == '\v');
}
static inline int isxdigit(int c) {
  return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static inline int isprint(int c) { return (c >= 32 && c <= 126); }
static inline int tolower(int c) {
  return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}
static inline int toupper(int c) {
  return (c >= 'a' && c <= 'z') ? (c - 32) : c;
}

#ifdef __cplusplus
}
#endif

#endif
