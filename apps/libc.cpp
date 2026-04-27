#include "include/syscall.h"
#include "include/types.h"
#undef _LIBC_SKIP_STANDARD_FUNCS
#include "stdio.h"
#include <string.h>
#include <stdint.h>

/* Forward declarations to avoid including standard headers that conflict with types.h */
extern "C" void *memcpy(void *dest, const void *src, size_t n);
extern "C" void *memset(void *s, int c, size_t n);

/* ============= GLOBAL STREAMS ============= */
static FILE _stdin_struct = {0, 1, 0, 0, 0, 0, 0, 0, 2}; /* _IONBF */
static FILE _stdout_struct = {1, 2, 0, 0, 0, 0, 0, 0, 2};
static FILE _stderr_struct = {2, 2, 0, 0, 0, 0, 0, 0, 2};

extern "C" {
    FILE *stdin = &_stdin_struct;
    FILE *stdout = &_stdout_struct;
    FILE *stderr = &_stderr_struct;
}

/* ============= POSIX WRAPPERS ============= */

extern "C" {

int open(const char *path, int flags, ...) {
    return syscall_open(path, flags);
}

int close(int fd) {
    return syscall_close(fd);
}

ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)syscall_read(fd, (void *)buf, (uint32_t)count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)syscall_write(fd, buf, (uint32_t)count);
}

int fork(void) {
    return syscall_fork();
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    return syscall_execve(path, (char **)argv, (char **)envp);
}

int wait(int *status) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(11), "b"(status));
    return res;
}

int chdir(const char *path) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(23), "b"(path));
    return res;
}

char *getcwd(char *buf, size_t size) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(26), "b"(buf), "c"(size));
    return res >= 0 ? buf : 0;
}

int unlink(const char *path) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(24), "b"(path));
    return res;
}

int mkdir(const char *path, mode_t mode) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(25), "b"(path), "c"(mode));
    return res;
}

int rmdir(const char *path) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(56), "b"(path));
    return res;
}

void _exit(int status) {
    syscall_exit(status);
    while(1);
}

void exit(int status) {
    _exit(status);
}

/* ============= HEAP ALLOCATOR (BUMP) ============= */

static uint8_t *heap_start = 0;
static uint8_t *heap_current = 0;
static uint8_t *heap_end = 0;

static void heap_init() {
    heap_start = (uint8_t *)syscall_sbrk(0);
    heap_current = heap_start;
    heap_end = heap_start;
}

void *malloc(size_t size) {
    if (!heap_start) heap_init();
    size = (size + 7) & ~7;
    uint8_t *new_end = heap_current + size;
    if (new_end > heap_end) {
        size_t grow = (size + 4095) & ~4095;
        void *result = (void *)syscall_sbrk((int)grow);
        if ((intptr_t)result < 0) return 0;
        heap_end += grow;
    }
    void *ptr = heap_current;
    heap_current = new_end;
    return ptr;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }
    void *new_ptr = malloc(size);
    if (!new_ptr) return 0;
    memcpy(new_ptr, ptr, size); 
    return new_ptr;
}

void free(void *ptr) {
    (void)ptr;
}

void perror(const char *s) {
    if (s && *s) {
        write(2, s, strlen(s));
        write(2, ": ", 2);
    }
    write(2, "Error\n", 6);
}

/* Directory functions */

DIR *opendir(const char *path) {
    int fd = open(path, 0);
    if (fd < 0) return 0;
    DIR *dir = (DIR *)malloc(sizeof(DIR));
    if (!dir) { close(fd); return 0; }
    dir->fd = fd;
    memset(&dir->de, 0, sizeof(struct dirent));
    return dir;
}

int closedir(DIR *dirp) {
    if (!dirp) return -1;
    int res = close(dirp->fd);
    free(dirp);
    return res;
}

struct dirent *readdir(DIR *dirp) {
    if (!dirp) return 0;
    int res;
    /* Retro-OS readdir(fd, index, de) returns 0 on success, <0 on end/error */
    /* We store the current index in de.d_off temporarily or just use a local counter? */
    /* Wait, I should add an index field to DIR! */
    static int dummy_index = 0; // This is BAD, DIR should have an index.
    
    // I will use de.d_off as the index.
    res = 0;
    asm volatile("int $0x80" : "=a"(res) : "a"(21), "b"(dirp->fd), "c"(dirp->de.d_off), "d"(&dirp->de));
    if (res < 0) return 0;
    dirp->de.d_off++;
    return &dirp->de;
}

void rewinddir(DIR *dirp) {
    if (dirp) dirp->de.d_off = 0;
}

int dirfd(DIR *dirp) {
    return dirp ? dirp->fd : -1;
}

} /* extern "C" */
