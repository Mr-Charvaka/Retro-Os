#ifndef FILE_OPS_H
#define FILE_OPS_H

#include "../include/types.h"
#include "../include/vfs.h"

// POSIX types
typedef int32_t ssize_t;
typedef int32_t off_t;

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Extended File Operations
// ============================================================================

// Read/write at offset
ssize_t sys_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t sys_pwrite(int fd, const void *buf, size_t count, off_t offset);

// Seek
off_t sys_lseek(int fd, off_t offset, int whence);

// Truncate
int sys_truncate(const char *path, off_t length);
int sys_ftruncate(int fd, off_t length);

// Links
int sys_link(const char *oldpath, const char *newpath);
int sys_symlink(const char *target, const char *linkpath);
ssize_t sys_readlink(const char *pathname, char *buf, size_t bufsiz);

// Stat variants
int sys_lstat(const char *pathname, struct stat *statbuf);

// *at() functions
int sys_mkdirat(int dirfd, const char *pathname, uint32_t mode);
int sys_unlinkat(int dirfd, const char *pathname, int flags);
int sys_renameat(int olddirfd, const char *oldpath, int newdirfd,
                 const char *newpath);

// Rename
int sys_rename(const char *oldpath, const char *newpath);

// Permissions
int sys_fchmod(int fd, uint32_t mode);
int sys_chown(const char *pathname, uint32_t owner, uint32_t group);
int sys_fchown(int fd, uint32_t owner, uint32_t group);

// File control
int sys_fcntl(int fd, int cmd, int arg);
int sys_ioctl(int fd, unsigned long request, void *argp);

// Directory
int sys_fchdir(int fd);

// Access
int sys_access(const char *pathname, int mode);
int sys_faccessat(int dirfd, const char *pathname, int mode, int flags);

#ifdef __cplusplus
}
#endif

#endif // FILE_OPS_H
