#ifndef _LIBC_H
#define _LIBC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"
#include "syscall.h"

#ifdef _LIBC_SKIP_STANDARD_FUNCS
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
/* #include <dirent.h> -- Newlib's dirent.h is a stub, we provide our own in types.h */
#else

#include "userlib.h"

#endif // _LIBC_SKIP_STANDARD_FUNCS

/* ============= POSIX LIB FUNCTIONS ============= */

int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int unlink(const char *path);
int rmdir(const char *path);
int mkdir(const char *path, mode_t mode);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);

int fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
int wait(int *status);

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

void perror(const char *s);

/* Directory functions */
DIR *opendir(const char *path);
int closedir(DIR *dirp);
struct dirent *readdir(DIR *dirp);
void rewinddir(DIR *dirp);
int dirfd(DIR *dirp);

/* ============ END POSIX ============= */

/* ============== STDLIB.H FUNCTIONS ============== */

/* Most string/utility functions are provided by userlib.h or string.h */

/* ============== SIGNAL NAMES ============== */

#ifndef _LIBC_SKIP_STANDARD_FUNCS
static inline char *strsignal(int sig) {
    return (char*)"SIG?";
}
#endif

#ifdef __cplusplus
}
#endif

#endif // _LIBC_H
