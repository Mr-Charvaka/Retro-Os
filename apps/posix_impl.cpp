#include "syscall.h"
#include "types.h"
#include <stddef.h>
#include <stdint.h>

extern "C" {

// ------------------- Memory Management -------------------
void *kmalloc(size_t size) {
  void *res;
  asm volatile("int $0x80" : "=a"(res) : "a"(6), "b"(size)); // SYS_SBRK = 6
  return res;
}

void kfree(void *ptr) { (void)ptr; }

// ------------------- String Functions -------------------
size_t strlen(const char *s) {
  size_t len = 0;
  if (!s)
    return 0;
  while (s[len])
    len++;
  return len;
}

char *strcpy(char *dest, const char *src) {
  char *d = dest;
  if (!dest || !src)
    return dest;
  while ((*dest++ = *src++))
    ;
  return d;
}

char *strcat(char *dest, const char *src) {
  char *d = dest;
  if (!dest || !src)
    return dest;
  while (*dest)
    dest++;
  while ((*dest++ = *src++))
    ;
  return d;
}

int strcmp(const char *s1, const char *s2) {
  if (!s1 || !s2)
    return s1 == s2 ? 0 : (s1 ? 1 : -1);
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

// POSIX File IO Wrappers
int open(const char *path, int flags, ...) {
  int res;
  asm volatile("int $0x80"
               : "=a"(res)
               : "a"(2), "b"(path), "c"(flags)); // SYS_OPEN = 2
  return res;
}

int close(int fd) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(5), "b"(fd)); // SYS_CLOSE = 5
  return res;
}

ssize_t read(int fd, void *buf, size_t count) {
  int res;
  asm volatile("int $0x80"
               : "=a"(res)
               : "a"(3), "b"(fd), "c"(buf), "d"(count)); // SYS_READ = 3
  return (ssize_t)res;
}

ssize_t write(int fd, const void *buf, size_t count) {
  int res;
  asm volatile("int $0x80"
               : "=a"(res)
               : "a"(4), "b"(fd), "c"(buf), "d"(count)); // SYS_WRITE = 4
  return (ssize_t)res;
}

int mkdir(const char *path, mode_t mode) {
  int res;
  asm volatile("int $0x80"
               : "=a"(res)
               : "a"(25), "b"(path), "c"(mode)); // SYS_MKDIR = 25
  return res;
}

int unlink(const char *path) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(24), "b"(path)); // SYS_UNLINK = 24
  return res;
}

// ------------------- Other POSIX Stubs -------------------
int fork() {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(9)); // SYS_FORK = 9
  return res;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
  int res;
  asm volatile("int $0x80"
               : "=a"(res)
               : "a"(10), "b"(path), "c"(argv), "d"(envp)); // SYS_EXECVE = 10
  return res;
}

void _exit(int status) {
  asm volatile("int $0x80" : : "a"(12), "b"(status)); // SYS_EXIT = 12
  while (1)
    ;
}

void exit(int status) { _exit(status); }

int wait(int *status) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(11), "b"(status)); // SYS_WAIT = 11
  return res;
}

int chdir(const char *path) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(23), "b"(path)); // SYS_CHDIR = 23
  return res;
}

char *getcwd(char *buf, size_t size) {
  int res;
  asm volatile("int $0x80"
               : "=a"(res)
               : "a"(26), "b"(buf), "c"(size)); // SYS_GETCWD = 26
  return res >= 0 ? buf : 0;
}

void *malloc(size_t size) { return kmalloc(size); }

void free(void *ptr) { kfree(ptr); }

int rmdir(const char *path) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(56), "b"(path)); // SYS_RMDIR = 56
  return res;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
  int res;
  asm volatile("int $0x80"
               : "=a"(res)
               : "a"(78), "b"(clk_id), "c"(tp)); // SYS_CLOCK_GETTIME = 78
  return res;
}

// ------------------- Pthreads & Semaphores Stubs -------------------
typedef uint32_t pthread_t;
int pthread_create(pthread_t *thread, const void *attr,
                   void *(*start_routine)(void *), void *arg) {
  int res;
  asm volatile("int $0x80"
               : "=a"(res)
               : "a"(96), "b"(thread), "c"(attr), "d"(start_routine), "S"(arg));
  return res;
}
int pthread_join(pthread_t thread, void **retval) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(98), "b"(thread), "c"(retval));
  return res;
}

typedef struct {
  uint32_t id;
  uint32_t value;
} sem_t;
int sem_init(sem_t *sem, int pshared, unsigned int value) {
  int res;
  asm volatile("int $0x80"
               : "=a"(res)
               : "a"(106), "b"(sem), "c"(pshared), "d"(value));
  return res;
}
int sem_wait(sem_t *sem) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(109), "b"(sem));
  return res;
}
int sem_post(sem_t *sem) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(110), "b"(sem));
  return res;
}
int sem_destroy(sem_t *sem) { return 0; }

typedef uint32_t sigset_t;
int sigemptyset(sigset_t *set) {
  if (set)
    *set = 0;
  return 0;
}
struct sigaction;
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
  int res;
  asm volatile("int $0x80"
               : "=a"(res)
               : "a"(69), "b"(sig), "c"(act), "d"(oldact));
  return res;
}
unsigned int alarm(unsigned int seconds) {
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(44), "b"(seconds));
  return (unsigned int)res;
}

} // extern "C"

// ------------------- C++ Global Operators -------------------
void *operator new(size_t size) { return kmalloc(size); }
void *operator new[](size_t size) { return kmalloc(size); }
void operator delete(void *ptr) noexcept { kfree(ptr); }
void operator delete[](void *ptr) noexcept { kfree(ptr); }
void operator delete(void *ptr, size_t) noexcept { kfree(ptr); }
void operator delete[](void *ptr, size_t) noexcept { kfree(ptr); }
