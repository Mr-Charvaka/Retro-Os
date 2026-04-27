#include <stdint.h>
#include <stddef.h>

extern "C" {

// Stubs for kernel link errors
void __assert_func(const char *file, int line, const char *func, const char *failedexpr) {
    // Hang or print error if we have a way
    while(1);
}

void *__memset_chk(void *dest, int c, size_t len, size_t destlen) {
    uint8_t *p = (uint8_t *)dest;
    while (len--) *p++ = (uint8_t)c;
    return dest;
}

// VFS Stubs (Needed by syscall.cpp but not implemented in VFS yet)
int truncate(const char *path, int length) { return -1; }
int ftruncate(int fd, int length) { return -1; }
int link(const char *oldpath, const char *newpath) { return -1; }
int symlink(const char *target, const char *linkpath) { return -1; }
int readlink(const char *path, char *buf, int bufsiz) { return -1; }
int lstat(const char *path, void *buf) { return -1; }
int mkdirat(int dirfd, const char *pathname, uint32_t mode) { return -1; }
int unlinkat(int dirfd, const char *pathname, int flags) { return -1; }
int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) { return -1; }

}
