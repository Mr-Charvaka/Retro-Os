#ifndef FCNTL_H
#define FCNTL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Base constants
#ifndef O_RDONLY
#define O_RDONLY 0x0000 
#define O_WRONLY 0x0001  
#define O_RDWR 0x0002    
#define O_ACCMODE 0x0003 
#endif

#ifndef O_CREAT
#define O_CREAT 0x0040
#endif
#ifndef O_EXCL
#define O_EXCL 0x0080
#endif
#ifndef O_NOCTTY
#define O_NOCTTY 0x0100
#endif
#ifndef O_TRUNC
#define O_TRUNC 0x0200
#endif
#ifndef O_APPEND
#define O_APPEND 0x0400
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x0800
#endif

#ifndef SEEK_SET
#define SEEK_SET 0 
#define SEEK_CUR 1 
#define SEEK_END 2 
#endif

#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif

#ifndef F_DUPFD
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_GETOWN 8
#define F_SETOWN 9
#endif

#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif

#ifndef F_DUPFD_CLOEXEC
#define F_DUPFD_CLOEXEC 0x406
#endif

#ifndef S_IFMT
#define S_IFMT 0170000   
#define S_IFSOCK 0140000 
#define S_IFLNK 0120000  
#define S_IFREG 0100000  
#define S_IFBLK 0060000  
#define S_IFDIR 0040000  
#define S_IFCHR 0020000  
#define S_IFIFO 0010000  
#endif

#if !defined(_SYS_FCNTL_H_) && !defined(_FCNTL_H_)
int open(const char *pathname, int flags, ...);
int openat(int dirfd, const char *pathname, int flags, ...);
int creat(const char *pathname, int mode);
int fcntl(int fd, int cmd, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif // FCNTL_H
