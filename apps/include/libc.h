#ifndef _LIBC_H
#define _LIBC_H

#ifdef _LIBC_SKIP_STANDARD_FUNCS
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include "syscall.h"

#else
#include "syscall.h"
#include "userlib.h"

#ifndef NULL
#define NULL 0
#endif

#ifndef offsetof
#define offsetof(type, member) ((uint32_t)&((type *)0)->member)
#endif

#ifdef __cplusplus
extern "C" {
#endif

int fork();
int execve(const char *path, char *const argv[], char *const envp[]);
int wait(int *status);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
void exit(int status);

int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int unlink(const char *path);
int rmdir(const char *path);
int mkdir(const char *path, mode_t mode);

void *malloc(size_t size);
void free(void *ptr);
static inline void *calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  void *p = malloc(total);
  if (p)
    memset(p, 0, total);
  return p;
}

#ifdef __cplusplus
}
#endif

/* ============== STDLIB.H FUNCTIONS ============== */

/* Absolute value */
#ifndef abs
#define abs(n) ((n) < 0 ? -(n) : (n))
#endif
static inline long labs(long n) { return n < 0 ? -n : n; }

/* Division result */
#ifndef _DIV_T_DEFINED
#define _DIV_T_DEFINED
typedef struct {
  int quot;
  int rem;
} div_t;
typedef struct {
  long quot;
  long rem;
} ldiv_t;
#endif

static inline div_t div(int numer, int denom) {
  div_t result;
  result.quot = numer / denom;
  result.rem = numer % denom;
  return result;
}

static inline ldiv_t ldiv(long numer, long denom) {
  ldiv_t result;
  result.quot = numer / denom;
  result.rem = numer % denom;
  return result;
}

/* String to long with base */
static inline long strtol(const char *s, char **endptr, int base) {
  long result = 0;
  int sign = 1;
  while (isspace(*s))
    s++;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+')
    s++;

  if (base == 0) {
    if (*s == '0') {
      s++;
      if (*s == 'x' || *s == 'X') {
        base = 16;
        s++;
      } else
        base = 8;
    } else
      base = 10;
  } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
  }

  while (*s) {
    int digit;
    if (*s >= '0' && *s <= '9')
      digit = *s - '0';
    else if (*s >= 'a' && *s <= 'z')
      digit = *s - 'a' + 10;
    else if (*s >= 'A' && *s <= 'Z')
      digit = *s - 'A' + 10;
    else
      break;
    if (digit >= base)
      break;
    result = result * base + digit;
    s++;
  }
  if (endptr)
    *endptr = (char *)s;
  return sign * result;
}

static inline unsigned long strtoul(const char *s, char **endptr, int base) {
  return (unsigned long)strtol(s, endptr, base);
}

/* Environment (stubs) */
static inline char *getenv(const char *name) {
  (void)name;
  return 0;
}
static inline int setenv(const char *n, const char *v, int o) {
  (void)n;
  (void)v;
  (void)o;
  return 0;
}
static inline int unsetenv(const char *name) {
  (void)name;
  return 0;
}

/* Quick sort */
static inline void qsort(void *base, uint32_t nmemb, uint32_t size,
                         int (*compar)(const void *, const void *)) {
  uint8_t *arr = (uint8_t *)base;
  for (uint32_t i = 0; i < nmemb - 1; i++) {
    for (uint32_t j = 0; j < nmemb - i - 1; j++) {
      if (compar(arr + j * size, arr + (j + 1) * size) > 0) {
        for (uint32_t k = 0; k < size; k++) {
          uint8_t tmp = arr[j * size + k];
          arr[j * size + k] = arr[(j + 1) * size + k];
          arr[(j + 1) * size + k] = tmp;
        }
      }
    }
  }
}

/* Binary search */
static inline void *bsearch(const void *key, const void *base, uint32_t nmemb,
                            uint32_t size,
                            int (*compar)(const void *, const void *)) {
  const uint8_t *arr = (const uint8_t *)base;
  uint32_t lo = 0, hi = nmemb;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    int cmp = compar(key, arr + mid * size);
    if (cmp == 0)
      return (void *)(arr + mid * size);
    if (cmp < 0)
      hi = mid;
    else
      lo = mid + 1;
  }
  return 0;
}

/* ============== CTYPE.H FUNCTIONS ============== */

static inline int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
static inline int isgraph(int c) { return c > 32 && c < 127; }
static inline int ispunct(int c) { return isgraph(c) && !isalnum(c); }
static inline int isblank(int c) { return c == ' ' || c == '\t'; }
static inline int isascii(int c) { return c >= 0 && c <= 127; }
static inline int toascii(int c) { return c & 0x7F; }

/* ============== STRING.H EXTENDED ============== */

static inline char *strncat(char *dest, const char *src, uint32_t n) {
  char *d = dest;
  while (*dest)
    dest++;
  while (n-- && *src)
    *dest++ = *src++;
  *dest = 0;
  return d;
}

static inline int strcasecmp(const char *s1, const char *s2) {
  while (*s1 && tolower(*s1) == tolower(*s2)) {
    s1++;
    s2++;
  }
  return tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2);
}

static inline int strncasecmp(const char *s1, const char *s2, uint32_t n) {
  while (n && *s1 && tolower(*s1) == tolower(*s2)) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0)
    return 0;
  return tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2);
}

#ifndef _LIBC_SKIP_STANDARD_FUNCS
static inline char *strdup(const char *s) {
  size_t len = strlen(s);
  char *new_s = (char *)malloc(len + 1);
  if (new_s)
    memcpy(new_s, s, len + 1);
  return new_s;
}

static inline char *strndup(const char *s, uint32_t n) {
  size_t len = strlen(s);
  if (len > n)
    len = n;
  char *new_s = (char *)malloc(len + 1);
  if (new_s) {
    memcpy(new_s, s, len);
    new_s[len] = '\0';
  }
  return new_s;
}
#endif

static inline uint32_t strspn(const char *s, const char *accept) {
  uint32_t count = 0;
  while (*s && strchr(accept, *s)) {
    count++;
    s++;
  }
  return count;
}

static inline uint32_t strcspn(const char *s, const char *reject) {
  uint32_t count = 0;
  while (*s && !strchr(reject, *s)) {
    count++;
    s++;
  }
  return count;
}

static inline char *strpbrk(const char *s, const char *accept) {
  while (*s) {
    if (strchr(accept, *s))
      return (char *)s;
    s++;
  }
  return 0;
}

#ifndef _LIBC_SKIP_STANDARD_FUNCS
static inline char *strtok_r(char *str, const char *delim, char **saveptr) {
  char *token;
  if (str == NULL)
    str = *saveptr;
  str += strspn(str, delim);
  if (*str == '\0') {
    *saveptr = str;
    return NULL;
  }
  token = str;
  str = strpbrk(token, delim);
  if (str == NULL) {
    *saveptr = strchr(token, '\0');
  } else {
    *str = '\0';
    *saveptr = str + 1;
  }
  return token;
}

static inline char *strtok(char *str, const char *delim) {
  static char *saveptr;
  return strtok_r(str, delim, &saveptr);
}

static inline void *memrchr(const void *s, int c, uint32_t n) {
  const unsigned char *cp = (const unsigned char *)s;
  for (cp += n; n > 0; n--) {
    if (*--cp == (unsigned char)c)
      return (void *)cp;
  }
  return NULL;
}
#endif

static inline void *memchr(const void *s, int c, uint32_t n) {
  const uint8_t *p = (const uint8_t *)s;
  while (n--) {
    if (*p == (uint8_t)c)
      return (void *)p;
    p++;
  }
  return 0;
}


/* ============== MATH.H FUNCTIONS (Integer) ============== */

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
static inline int clamp(int v, int lo, int hi) {
  return min(max(v, lo), hi);
}

/* Integer square root */
static inline uint32_t isqrt(uint32_t n) {
  if (n == 0)
    return 0;
  uint32_t x = n, y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + n / x) / 2;
  }
  return x;
}

/* Power */
static inline long ipow(long base, int exp) {
  long result = 1;
  while (exp > 0) {
    if (exp & 1)
      result *= base;
    base *= base;
    exp >>= 1;
  }
  return result;
}

/* GCD and LCM */
static inline int gcd(int a, int b) {
  a = abs(a);
  b = abs(b);
  while (b) {
    int t = b;
    b = a % b;
    a = t;
  }
  return a;
}

static inline int lcm(int a, int b) {
  if (a == 0 || b == 0)
    return 0;
  return abs(a / gcd(a, b) * b);
}

/* ============== ASSERT ============== */

#define assert(expr)                                                           \
  do {                                                                         \
    if (!(expr)) {                                                             \
      print("ASSERT FAILED: " #expr "\n");                                     \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

/* ============== ERRNO ============== */

static int errno_val = 0;
#define errno errno_val

#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EPIPE 32
#define EDOM 33
#define ERANGE 34
#define ENOSYS 38

static inline const char *strerror(int errnum) {
  switch (errnum) {
  case 0:
    return "Success";
  case EPERM:
    return "Operation not permitted";
  case ENOENT:
    return "No such file or directory";
  case EIO:
    return "I/O error";
  case EBADF:
    return "Bad file descriptor";
  case ENOMEM:
    return "Out of memory";
  case EACCES:
    return "Permission denied";
  case EEXIST:
    return "File exists";
  case ENOTDIR:
    return "Not a directory";
  case EISDIR:
    return "Is a directory";
  case EINVAL:
    return "Invalid argument";
  case ENOSPC:
    return "No space left on device";
  case ENOSYS:
    return "Function not implemented";
  default:
    return "Unknown error";
  }
}

/* ============== LIMITS.H ============== */

#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#define SHRT_MIN (-32768)
#define SHRT_MAX 32767
#define USHRT_MAX 65535
#define INT_MIN (-2147483647 - 1)
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U
#define LONG_MIN INT_MIN
#define LONG_MAX INT_MAX
#define ULONG_MAX UINT_MAX
#define LLONG_MIN (-9223372036854775807LL - 1)
#define LLONG_MAX 9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL
#define PATH_MAX 256
#define NAME_MAX 128

/* ============== STDDEF.H ============== */

#ifndef NULL
#define NULL 0
#endif

#ifndef offsetof
#define offsetof(type, member) ((uint32_t)&((type *)0)->member)
#endif

/* ============== POSIX PROTOTYPES ============== */
#ifdef __cplusplus
extern "C" {
#endif

int fork();
int execve(const char *path, char *const argv[], char *const envp[]);
int wait(int *status);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
void exit(int status);

int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int unlink(const char *path);
int rmdir(const char *path);
int mkdir(const char *path, mode_t mode);

void *malloc(size_t size);
void free(void *ptr);

#ifdef __cplusplus
}
#endif

/* ============== SETJMP (Simplified) ============== */

typedef int jmp_buf[6]; /* EBX, ESI, EDI, EBP, ESP, EIP */

/* Note: Real setjmp/longjmp need assembly. These are stubs. */
static inline int setjmp(jmp_buf env) {
  (void)env;
  return 0;
}
static inline void longjmp(jmp_buf env, int val) {
  (void)env;
  (void)val;
  exit(1);
}

/* ============== SIGNAL NAMES ============== */

#ifndef _LIBC_SKIP_STANDARD_FUNCS
static inline const char *strsignal(int sig) {
  switch (sig) {
  case 1:
    return "SIGHUP";
  case 2:
    return "SIGINT";
  case 3:
    return "SIGQUIT";
  case 4:
    return "SIGILL";
  case 5:
    return "SIGTRAP";
  case 6:
    return "SIGABRT";
  case 8:
    return "SIGFPE";
  case 9:
    return "SIGKILL";
  case 11:
    return "SIGSEGV";
  case 13:
    return "SIGPIPE";
  case 14:
    return "SIGALRM";
  case 15:
    return "SIGTERM";
  default:
    return "Unknown Signal";
  }
}
#endif

/* ============== TIME FUNCTIONS ============== */

// typedef long time_t; // Now in types.h

struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};

/* Time stubs */
static inline time_t time(time_t *t) {
  if (t)
    *t = 0;
  return 0;
}

static inline struct tm *localtime(const time_t *timer) {
  static struct tm tm;
  memset(&tm, 0, sizeof(struct tm));
  (void)timer;
  return &tm;
}

static inline uint32_t strftime(char *s, uint32_t max, const char *fmt,
                                const struct tm *tm) {
  char *p = s;
  while (*fmt && (uint32_t)(p - s) < max - 1) {
    if (*fmt == '%') {
      fmt++;
      char buf[16];
      switch (*fmt) {
      case 'Y':
        itoa(tm->tm_year + 1900, buf, 10);
        break;
      case 'm':
        format_num(buf, tm->tm_mon + 1, 2);
        break;
      case 'd':
        format_num(buf, tm->tm_mday, 2);
        break;
      case 'H':
        format_num(buf, tm->tm_hour, 2);
        break;
      case 'M':
        format_num(buf, tm->tm_min, 2);
        break;
      case 'S':
        format_num(buf, tm->tm_sec, 2);
        break;
      case 'a':
        strcpy(buf, day_names[tm->tm_wday]);
        break;
      case 'b':
        strcpy(buf, month_names[tm->tm_mon]);
        break;
      case '%':
        buf[0] = '%';
        buf[1] = 0;
        break;
      default:
        buf[0] = *fmt;
        buf[1] = 0;
      }
      for (char *b = buf; *b && (uint32_t)(p - s) < max - 1;)
        *p++ = *b++;
      fmt++;
    } else {
      *p++ = *fmt++;
    }
  }
  *p = 0;
  return p - s;
}

/* ============== CTIME ============== */

static inline char *ctime(const time_t *t) {
  static char buf[32];
  struct tm *tm = localtime(t);
  strftime(buf, 32, "%a %b %d %H:%M:%S %Y", tm);
  return buf;
}

static inline char *asctime(const struct tm *tm) {
  static char buf[32];
  strftime(buf, 32, "%a %b %d %H:%M:%S %Y", tm);
  return buf;
}

#endif // _LIBC_SKIP_STANDARD_FUNCS

#endif /* _LIBC_H */
