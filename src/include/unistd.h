#ifndef UNISTD_H
#define UNISTD_H

#include "types.h"

// POSIX types not in types.h
typedef int32_t ssize_t;
typedef int32_t off_t;

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Standard File Descriptors
// ============================================================================
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// ============================================================================
// Access Mode Constants (for access())
// ============================================================================
#define F_OK 0 // File exists
#define X_OK 1 // Execute permission
#define W_OK 2 // Write permission
#define R_OK 4 // Read permission

// ============================================================================
// Seek Constants (for lseek())
// ============================================================================
#define SEEK_SET 0 // Seek from beginning
#define SEEK_CUR 1 // Seek from current position
#define SEEK_END 2 // Seek from end

// ============================================================================
// sysconf() Constants
// ============================================================================
#define _SC_ARG_MAX 0
#define _SC_CHILD_MAX 1
#define _SC_CLK_TCK 2
#define _SC_NGROUPS_MAX 3
#define _SC_OPEN_MAX 4
#define _SC_STREAM_MAX 5
#define _SC_TZNAME_MAX 6
#define _SC_JOB_CONTROL 7
#define _SC_SAVED_IDS 8
#define _SC_VERSION 9
#define _SC_PAGESIZE 10
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_CONF 11
#define _SC_NPROCESSORS_ONLN 12
#define _SC_PHYS_PAGES 13
#define _SC_AVPHYS_PAGES 14

// ============================================================================
// pathconf() / fpathconf() Constants
// ============================================================================
#define _PC_LINK_MAX 0
#define _PC_MAX_CANON 1
#define _PC_MAX_INPUT 2
#define _PC_NAME_MAX 3
#define _PC_PATH_MAX 4
#define _PC_PIPE_BUF 5
#define _PC_CHOWN_RESTRICTED 6
#define _PC_NO_TRUNC 7
#define _PC_VDISABLE 8

// ============================================================================
// Process Functions
// ============================================================================
int getpid(void);
int getppid(void);
int getpgid(int pid);
int setpgid(int pid, int pgid);
int getpgrp(void);
int setpgrp(void);
int getsid(int pid);
int setsid(void);

int fork(void);
int execve(const char *pathname, char *const argv[], char *const envp[]);
int execv(const char *pathname, char *const argv[]);
int execvp(const char *file, char *const argv[]);
void _exit(int status);

// ============================================================================
// User/Group ID Functions
// ============================================================================
int getuid(void);
int geteuid(void);
int getgid(void);
int getegid(void);
int setuid(int uid);
int seteuid(int euid);
int setgid(int gid);
int setegid(int egid);

// ============================================================================
// File Operations
// ============================================================================
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
off_t lseek(int fd, off_t offset, int whence);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);

int unlink(const char *pathname);
int rmdir(const char *pathname);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

int access(const char *pathname, int mode);
int chown(const char *pathname, int owner, int group);
int fchown(int fd, int owner, int group);
int truncate(const char *path, off_t length);
int ftruncate(int fd, off_t length);

// ============================================================================
// Directory Functions
// ============================================================================
int chdir(const char *path);
int fchdir(int fd);
char *getcwd(char *buf, size_t size);

// ============================================================================
// Miscellaneous
// ============================================================================
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
unsigned int alarm(unsigned int seconds);
int pause(void);

int gethostname(char *name, size_t len);
int sethostname(const char *name, size_t len);

long sysconf(int name);
long pathconf(const char *path, int name);
long fpathconf(int fd, int name);

int isatty(int fd);
char *ttyname(int fd);

#ifdef __cplusplus
}
#endif

#endif // UNISTD_H
