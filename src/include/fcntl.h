#ifndef FCNTL_H
#define FCNTL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// File Open Flags (for open/openat)
// ============================================================================
#define O_RDONLY 0x0000  // Open for reading only
#define O_WRONLY 0x0001  // Open for writing only
#define O_RDWR 0x0002    // Open for reading and writing
#define O_ACCMODE 0x0003 // Mask for access mode

#define O_CREAT 0x0040      // Create file if it doesn't exist
#define O_EXCL 0x0080       // Fail if O_CREAT and file exists
#define O_NOCTTY 0x0100     // Don't make terminal the controlling tty
#define O_TRUNC 0x0200      // Truncate file to 0 length
#define O_APPEND 0x0400     // Append mode
#define O_NONBLOCK 0x0800   // Non-blocking I/O
#define O_SYNC 0x1000       // Synchronous writes
#define O_ASYNC 0x2000      // Asynchronous I/O
#define O_DIRECT 0x4000     // Direct I/O
#define O_LARGEFILE 0x8000  // Allow large files
#define O_DIRECTORY 0x10000 // Must be a directory
#define O_NOFOLLOW 0x20000  // Don't follow symlinks
#define O_CLOEXEC 0x80000   // Close on exec
#define O_NOATIME 0x40000   // Don't update access time

// ============================================================================
// fcntl() Commands
// ============================================================================
#define F_DUPFD 0             // Duplicate file descriptor
#define F_GETFD 1             // Get file descriptor flags
#define F_SETFD 2             // Set file descriptor flags
#define F_GETFL 3             // Get file status flags
#define F_SETFL 4             // Set file status flags
#define F_GETLK 5             // Get record locking info
#define F_SETLK 6             // Set record locking info (non-blocking)
#define F_SETLKW 7            // Set record locking info (blocking)
#define F_SETOWN 8            // Set owner for SIGIO
#define F_GETOWN 9            // Get owner for SIGIO
#define F_DUPFD_CLOEXEC 0x406 // Duplicate with close-on-exec

// File descriptor flags (for F_GETFD/F_SETFD)
#define FD_CLOEXEC 1 // Close on exec

// ============================================================================
// File Lock Structure
// ============================================================================
struct flock {
  short l_type;   // Type of lock: F_RDLCK, F_WRLCK, F_UNLCK
  short l_whence; // Starting point: SEEK_SET, SEEK_CUR, SEEK_END
  int l_start;    // Starting offset
  int l_len;      // Length: 0 = lock to EOF
  int l_pid;      // Process holding lock
};

// Lock types
#define F_RDLCK 0 // Read lock
#define F_WRLCK 1 // Write lock
#define F_UNLCK 2 // Unlock

// ============================================================================
// Seek Constants
// ============================================================================
#define SEEK_SET 0 // Seek from beginning
#define SEEK_CUR 1 // Seek from current position
#define SEEK_END 2 // Seek from end

// ============================================================================
// AT_* Constants for *at() functions
// ============================================================================
#define AT_FDCWD -100             // Use current working directory
#define AT_SYMLINK_NOFOLLOW 0x100 // Don't follow symlinks
#define AT_REMOVEDIR 0x200        // Remove directory instead of file
#define AT_SYMLINK_FOLLOW 0x400   // Follow symlinks
#define AT_EACCESS 0x200          // Use effective IDs for access check

// ============================================================================
// File Mode Bits
// ============================================================================
#define S_IFMT 0170000   // Type mask
#define S_IFSOCK 0140000 // Socket
#define S_IFLNK 0120000  // Symbolic link
#define S_IFREG 0100000  // Regular file
#define S_IFBLK 0060000  // Block device
#define S_IFDIR 0040000  // Directory
#define S_IFCHR 0020000  // Character device
#define S_IFIFO 0010000  // FIFO

#define S_ISUID 0004000 // Set UID bit
#define S_ISGID 0002000 // Set GID bit
#define S_ISVTX 0001000 // Sticky bit

#define S_IRWXU 0000700 // Owner RWX
#define S_IRUSR 0000400 // Owner read
#define S_IWUSR 0000200 // Owner write
#define S_IXUSR 0000100 // Owner execute

#define S_IRWXG 0000070 // Group RWX
#define S_IRGRP 0000040 // Group read
#define S_IWGRP 0000020 // Group write
#define S_IXGRP 0000010 // Group execute

#define S_IRWXO 0000007 // Others RWX
#define S_IROTH 0000004 // Others read
#define S_IWOTH 0000002 // Others write
#define S_IXOTH 0000001 // Others execute

// File type test macros
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

// ============================================================================
// Function Declarations
// ============================================================================
int open(const char *pathname, int flags, ...);
int openat(int dirfd, const char *pathname, int flags, ...);
int creat(const char *pathname, int mode);
int fcntl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif // FCNTL_H
