#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <reent.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>

// Newlib internal macro for the current reentrancy structure
#ifndef _REENT
#define _REENT (__getreent())
#endif

// Official Newlib return type for read/write
typedef _ssize_t real_ssize_t;

extern "C" {

// --- Newlib Reentrancy Support ---
static int _errno_val = 0;
int *__errno(void) { return &_errno_val; }

unsigned char _static_reent[1024]; 
struct _reent *_impure_ptr = (struct _reent *)_static_reent;

struct _reent *__getreent(void) {
    return _impure_ptr;
}

// --- Malloc/Free Bridge ---
extern void *_malloc_r(struct _reent *ptr, size_t size);
extern void _free_r(struct _reent *ptr, void *addr);
extern void *_realloc_r(struct _reent *ptr, void *addr, size_t size);
extern void *_calloc_r(struct _reent *ptr, size_t n, size_t size);

void *malloc(size_t size) { return _malloc_r(_REENT, size); }
void free(void *ptr) { _free_r(_REENT, ptr); }
void *realloc(void *ptr, size_t size) { return _realloc_r(_REENT, ptr, size); }
void *calloc(size_t n, size_t size) { return _calloc_r(_REENT, n, size); }

// --- Pure System Call Bridge ---

void *_sbrk(ptrdiff_t incr) {
    void *res;
    asm volatile("int $0x80" : "=a"(res) : "a"(6), "b"(incr));
    return res;
}

real_ssize_t _write(int file, const void *ptr, size_t len) {
    real_ssize_t res;
    asm volatile("int $0x80" : "=a"(res) : "a"(4), "b"(file), "c"(ptr), "d"(len));
    return res;
}

real_ssize_t _read(int file, void *ptr, size_t len) {
    real_ssize_t res;
    asm volatile("int $0x80" : "=a"(res) : "a"(3), "b"(file), "c"(ptr), "d"(len));
    return res;
}

int _open(const char *name, int flags, ...) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(2), "b"(name), "c"(flags));
    return res;
}

int _close(int file) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(5), "b"(file));
    return res;
}

off_t _lseek(int file, off_t ptr, int dir) {
    off_t res;
    asm volatile("int $0x80" : "=a"(res) : "a"(18), "b"(file), "c"(ptr), "d"(dir));
    return res;
}

void _exit(int status) {
    asm volatile("int $0x80" : : "a"(12), "b"(status));
    while(1);
}

int _getpid() {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(20));
    return res;
}

int _kill(int pid, int sig) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(19), "b"(pid), "c"(sig));
    return res;
}

int _isatty(int file) {
    return (file >= 0 && file <= 2);
}

// --- Reentrant Glue (Using official _ssize_t compatibility) ---
void *_sbrk_r(struct _reent *r, ptrdiff_t incr) { return _sbrk(incr); }
real_ssize_t _write_r(struct _reent *r, int fd, const void *ptr, size_t len) { return _write(fd, ptr, len); }
real_ssize_t _read_r(struct _reent *r, int fd, void *ptr, size_t len) { return _read(fd, ptr, len); }
int _close_r(struct _reent *r, int fd) { return _close(fd); }
off_t _lseek_r(struct _reent *r, int fd, off_t offset, int whence) { return _lseek(fd, offset, whence); }
int _open_r(struct _reent *r, const char *path, int flags, int mode) { return _open(path, flags, mode); }
int _isatty_r(struct _reent *r, int fd) { return _isatty(fd); }
int _kill_r(struct _reent *r, int pid, int sig) { return _kill(pid, sig); }
int _getpid_r(struct _reent *r) { return _getpid(); }

#ifndef S_IFDIR
#define S_IFDIR 0x4000
#endif
#ifndef S_IFREG
#define S_IFREG 0x8000
#endif

struct kernel_stat_pkg {
  uint32_t st_dev, st_ino, st_mode, st_nlink, st_uid, st_gid, st_rdev, st_size;
  uint32_t k_atime, k_mtime, k_ctime, st_blksize, st_blocks;
};

int _fstat(int file, struct stat *st) {
    struct kernel_stat_pkg kst;
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(28), "b"(file), "c"(&kst));
    if (res < 0) return -1;
    memset(st, 0, sizeof(struct stat));
    st->st_size = kst.st_size;
    st->st_mode = (kst.st_mode == 2 ? S_IFDIR : S_IFREG) | 0777;
    return 0;
}
int _fstat_r(struct _reent *r, int fd, struct stat *st) { return _fstat(fd, st); }

// --- Professional Buffered I/O (FILE structure) ---
#define FILE_BUF_SIZE 1024
struct FILE_impl {
    int fd;
    char buffer[FILE_BUF_SIZE];
    int pos;
    int size;
    int eof;
    int error;
    int mode; // 0=read, 1=write
};

static FILE_impl _files[16];

#undef stdin
#undef stdout
#undef stderr
FILE *stdin  = (FILE*)&_files[0];
FILE *stdout = (FILE*)&_files[1];
FILE *stderr = (FILE*)&_files[2];

void _init_stdio() {
    memset(_files, 0, sizeof(_files));
    _files[0].fd = 0; _files[0].mode = 0;
    _files[1].fd = 1; _files[1].mode = 1;
    _files[2].fd = 2; _files[2].mode = 1;
}

FILE *fopen(const char *path, const char *mode) {
    int flags = 0;
    if (mode[0] == 'r') flags = 0;
    else if (mode[0] == 'w') flags = 1 | 0x100;
    int fd = _open(path, flags);
    if (fd < 0) return NULL;
    for (int i = 3; i < 16; i++) {
        if (_files[i].fd == 0) {
            _files[i].fd = fd;
            _files[i].mode = (mode[0] == 'r' ? 0 : 1);
            _files[i].pos = 0;
            return (FILE*)&_files[i];
        }
    }
    _close(fd); return NULL;
}

int fflush(FILE *stream) {
    FILE_impl *f = (FILE_impl*)stream;
    if (f->mode == 1 && f->pos > 0) { _write(f->fd, f->buffer, f->pos); f->pos = 0; }
    return 0;
}

int fclose(FILE *stream) {
    fflush(stream); FILE_impl *f = (FILE_impl*)stream; _close(f->fd); f->fd = 0; return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    FILE_impl *f = (FILE_impl*)stream;
    size_t total = size * nmemb;
    real_ssize_t n = _read(f->fd, ptr, total);
    return (n > 0) ? n / size : 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    FILE_impl *f = (FILE_impl*)stream;
    size_t total = size * nmemb;
    if (f->fd == 1 || f->fd == 2) {
        const char *src = (const char *)ptr;
        for (size_t i = 0; i < total; i++) {
            f->buffer[f->pos++] = src[i];
            if (src[i] == '\n' || f->pos == FILE_BUF_SIZE) fflush(stream);
        }
        return nmemb;
    }
    _write(f->fd, ptr, total); return nmemb;
}

static char *_internal_env[] = { (char*)"USER=root", (char*)"HOME=/", (char*)"PATH=/apps:/bin", nullptr };
char **environ = _internal_env;

char *getenv(const char *name) {
    int len = strlen(name);
    for (char **p = environ; *p; p++) {
        if (strncmp(*p, name, len) == 0 && (*p)[len] == '=') return *p + len + 1;
    }
    return NULL;
}

#undef vsnprintf
#undef snprintf
#undef sprintf
#undef printf
#undef vprintf

static int out_char(char **str, char *end, char c) {
    if (str && *str < end) { **str = c; (*str)++; }
    return 1;
}

static int print_num(char **str, char *end, uint32_t n, int base, int width, int pad, int upper) {
    char buf[32]; int count = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { buf[count++] = digits[n % base]; n /= base; } while (n);
    int written = 0;
    while (width > count) { written += out_char(str, end, pad); width--; }
    while (count > 0) { written += out_char(str, end, buf[--count]); }
    return written;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (size == 0) return 0;
    char *end = str + size - 1; char *p = str; int written = 0;
    while (*format) {
        if (*format == '%') {
            format++;
            int width = 0, pad = ' ', precision = -1;
            if (*format == '0') { pad = '0'; format++; }
            while (*format >= '0' && *format <= '9') { width = width * 10 + (*format - '0'); format++; }
            if (*format == '.') {
                format++;
                if (*format == '*') {
                    precision = va_arg(ap, int);
                    format++;
                } else {
                    precision = 0;
                    while (*format >= '0' && *format <= '9') { precision = precision * 10 + (*format - '0'); format++; }
                }
            }
            if (*format == 'l') format++;
            switch (*format) {
                case 'd': case 'i': {
                    int n = va_arg(ap, int);
                    if (n < 0) { written += out_char(&p, end, '-'); n = -n; }
                    written += print_num(&p, end, n, 10, width, pad, 0); break;
                }
                case 'u': written += print_num(&p, end, va_arg(ap, unsigned int), 10, width, pad, 0); break;
                case 'x': written += print_num(&p, end, va_arg(ap, unsigned int), 16, width, pad, 0); break;
                case 'X': written += print_num(&p, end, va_arg(ap, unsigned int), 16, width, pad, 1); break;
                case 'p': {
                    written += out_char(&p, end, '0'); written += out_char(&p, end, 'x');
                    written += print_num(&p, end, va_arg(ap, unsigned int), 16, 8, '0', 0); break;
                }
                case 's': {
                    char *s = va_arg(ap, char *); if (!s) s = (char *)"(null)";
                    int slen = 0;
                    while (s[slen] && (precision < 0 || slen < precision)) {
                        written += out_char(&p, end, s[slen++]);
                    }
                    break;
                }
                case 'c': written += out_char(&p, end, (char)va_arg(ap, int)); break;
                default: written += out_char(&p, end, *format); break;
            }
        } else { written += out_char(&p, end, *format); }
        format++;
    }
    if (str) *p = '\0'; return written;
}

int printf(const char *format, ...) {
    va_list ap; va_start(ap, format);
    char buf[1024]; int len = vsnprintf(buf, sizeof(buf), format, ap);
    fwrite(buf, 1, len, stdout);
    va_end(ap); return len;
}

int puts(const char *s) {
    int len = strlen(s); fwrite(s, 1, len, stdout); fwrite("\n", 1, 1, stdout);
    return len + 1;
}

char *fgets(char *s, int size, FILE *stream) {
    int i = 0;
    while (i < size - 1) {
        char c; if (fread(&c, 1, 1, stream) <= 0) break;
        s[i++] = c; if (c == '\n') break;
    }
    if (i == 0) return NULL;
    s[i] = '\0'; return s;
}

int fputs(const char *s, FILE *stream) {
    int len = strlen(s);
    return fwrite(s, 1, len, stream);
}

int fork() {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(9));
    return res;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(10), "b"(path), "c"(argv), "d"(envp));
    return res;
}

void exit(int status) { _exit(status); }

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
    return res >= 0 ? buf : NULL;
}

int open(const char *path, int flags, ...) { return _open(path, flags, 0); }
int close(int fd) { return _close(fd); }
real_ssize_t read(int fd, void *buf, size_t count) { return _read(fd, buf, count); }
real_ssize_t write(int fd, const void *buf, size_t count) { return _write(fd, buf, count); }

int unlink(const char *path) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(24), "b"(path));
    return res;
}

int mkdir(const char *path, uint32_t mode) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(25), "b"(path), "c"(mode));
    return res;
}

int rmdir(const char *path) {
    int res;
    asm volatile("int $0x80" : "=a"(res) : "a"(56), "b"(path));
    return res;
}

void __stack_chk_fail(void) { puts("STACK SMASH DETECTED!"); _exit(1); }
void __assert_func(const char *f, int l, const char *func, const char *expr) {
    printf("ASSERTION FAILED: %s at %s:%d in %s\n", expr, f, l, func);
    _exit(1);
}
void __malloc_lock(struct _reent *) {}
void __malloc_unlock(struct _reent *) {}

extern "C" __attribute__((weak)) void _start() {
    _init_stdio();
    extern int main(int argc, char **argv, char **envp) __attribute__((weak));
    if (main) {
        int ret = main(0, nullptr, (char**)environ);
        fflush(stdout);
        _exit(ret);
    }
    _exit(0);
}

} // extern "C"

void *operator new(size_t size) { return malloc(size); }
void *operator new[](size_t size) { return malloc(size); }
void operator delete(void *ptr) noexcept { free(ptr); }
void operator delete[](void *ptr) noexcept { free(ptr); }
void operator delete(void *ptr, size_t) noexcept { free(ptr); }
void operator delete[](void *ptr, size_t) noexcept { free(ptr); }
void* __cxa_pure_virtual = 0;
extern "C" void __cxa_atexit() {}
