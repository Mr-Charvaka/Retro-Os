// ============================================================================
// file_ops.cpp - Extended POSIX File Operations
// Entirely hand-crafted for Retro-OS kernel
// ============================================================================

#include "../drivers/serial.h"
#include "../include/errno.h"
#include "../include/fcntl.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "heap.h"
#include "memory.h"
#include "process.h"


extern "C" {

// ============================================================================
// File Position Tracking (per file descriptor)
// ============================================================================
static uint32_t fd_positions[256]; // Simplified: global tracking

// ============================================================================
// sys_pread - Read from file at offset without changing position
// ============================================================================

ssize_t sys_pread(int fd, void *buf, size_t count, off_t offset) {
  if (!buf)
    return -EFAULT;

  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  vfs_node_t *node = current_process->fd_table[fd];
  if (!node->read)
    return -EINVAL;

  return (ssize_t)node->read(node, (uint32_t)offset, (uint32_t)count,
                             (uint8_t *)buf);
}

// ============================================================================
// sys_pwrite - Write to file at offset without changing position
// ============================================================================

ssize_t sys_pwrite(int fd, const void *buf, size_t count, off_t offset) {
  if (!buf)
    return -EFAULT;

  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  vfs_node_t *node = current_process->fd_table[fd];
  if (!node->write)
    return -EINVAL;

  return (ssize_t)node->write(node, (uint32_t)offset, (uint32_t)count,
                              (uint8_t *)buf);
}

// ============================================================================
// sys_lseek - Reposition read/write file offset
// ============================================================================

off_t sys_lseek(int fd, off_t offset, int whence) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  vfs_node_t *node = current_process->fd_table[fd];

  // Get current position
  uint32_t current_pos = fd_positions[fd];
  uint32_t new_pos;

  switch (whence) {
  case SEEK_SET:
    new_pos = offset;
    break;
  case SEEK_CUR:
    new_pos = current_pos + offset;
    break;
  case SEEK_END:
    new_pos = node->length + offset;
    break;
  default:
    return -EINVAL;
  }

  // Check for negative position
  if ((int32_t)new_pos < 0)
    return -EINVAL;

  fd_positions[fd] = new_pos;
  return (off_t)new_pos;
}

// ============================================================================
// sys_truncate - Truncate a file to specified length
// ============================================================================

int sys_truncate(const char *path, off_t length) {
  if (!path)
    return -EFAULT;

  vfs_node_t *node = vfs_resolve_path(path);
  if (!node)
    return -ENOENT;

  // Update file length (simplified - actual implementation would need
  // filesystem support)
  if ((node->flags & 0x7) != VFS_FILE)
    return -EISDIR;

  node->length = (uint32_t)length;
  return 0;
}

// ============================================================================
// sys_ftruncate - Truncate file via file descriptor
// ============================================================================

int sys_ftruncate(int fd, off_t length) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  vfs_node_t *node = current_process->fd_table[fd];
  if ((node->flags & 0x7) != VFS_FILE)
    return -EINVAL;

  node->length = (uint32_t)length;
  return 0;
}

// ============================================================================
// sys_link - Create a hard link
// ============================================================================

int sys_link(const char *oldpath, const char *newpath) {
  if (!oldpath || !newpath)
    return -EFAULT;

  // Verify source exists
  vfs_node_t *src = vfs_resolve_path(oldpath);
  if (!src)
    return -ENOENT;

  // Hard links not fully supported in our simple VFS
  // Return stub error
  serial_log("LINK: Hard links not implemented");
  return -ENOSYS;
}

// ============================================================================
// sys_symlink - Create a symbolic link
// ============================================================================

int sys_symlink(const char *target, const char *linkpath) {
  if (!target || !linkpath)
    return -EFAULT;

  // Find parent directory
  char parent[256];
  char linkname[128];

  // Extract parent directory and link name
  int len = strlen(linkpath);
  int last_slash = -1;
  for (int i = len - 1; i >= 0; i--) {
    if (linkpath[i] == '/') {
      last_slash = i;
      break;
    }
  }

  if (last_slash == -1) {
    strcpy(parent, "/");
    strcpy(linkname, linkpath);
  } else {
    strncpy(parent, linkpath, last_slash);
    parent[last_slash] = 0;
    strcpy(linkname, linkpath + last_slash + 1);
  }

  vfs_node_t *parent_node = vfs_resolve_path(parent);
  if (!parent_node)
    return -ENOENT;

  // Symbolic links not fully implemented
  serial_log("SYMLINK: Symbolic links not implemented");
  return -ENOSYS;
}

// ============================================================================
// sys_readlink - Read value of symbolic link
// ============================================================================

ssize_t sys_readlink(const char *pathname, char *buf, size_t bufsiz) {
  if (!pathname || !buf)
    return -EFAULT;

  vfs_node_t *node = vfs_resolve_path(pathname);
  if (!node)
    return -ENOENT;

  if ((node->flags & 0x7) != VFS_SYMLINK)
    return -EINVAL;

  // In a real implementation, we'd read the symlink target
  // For now, return not implemented
  return -ENOSYS;
}

// ============================================================================
// sys_lstat - Get symbolic link status (don't follow symlinks)
// ============================================================================

int sys_lstat(const char *pathname, struct stat *statbuf) {
  if (!pathname || !statbuf)
    return -EFAULT;

  // For now, identical to stat (we don't chase symlinks yet)
  vfs_node_t *node = vfs_resolve_path(pathname);
  if (!node)
    return -ENOENT;

  statbuf->st_dev = 0;
  statbuf->st_ino = node->inode;
  statbuf->st_mode = node->mask;

  // Set file type bits
  switch (node->flags & 0x7) {
  case VFS_FILE:
    statbuf->st_mode |= S_IFREG;
    break;
  case VFS_DIRECTORY:
    statbuf->st_mode |= S_IFDIR;
    break;
  case VFS_CHARDEVICE:
    statbuf->st_mode |= S_IFCHR;
    break;
  case VFS_BLOCKDEVICE:
    statbuf->st_mode |= S_IFBLK;
    break;
  case VFS_PIPE:
    statbuf->st_mode |= S_IFIFO;
    break;
  case VFS_SYMLINK:
    statbuf->st_mode |= S_IFLNK;
    break;
  case VFS_SOCKET:
    statbuf->st_mode |= S_IFSOCK;
    break;
  }

  statbuf->st_nlink = 1;
  statbuf->st_uid = node->uid;
  statbuf->st_gid = node->gid;
  statbuf->st_rdev = 0;
  statbuf->st_size = node->length;
  statbuf->st_atime = 0;
  statbuf->st_mtime = 0;
  statbuf->st_ctime = 0;
  statbuf->st_blksize = 512;
  statbuf->st_blocks = (node->length + 511) / 512;

  return 0;
}

// ============================================================================
// sys_mkdirat - Create directory relative to directory fd
// ============================================================================

int sys_mkdirat(int dirfd, const char *pathname, uint32_t mode) {
  if (!pathname)
    return -EFAULT;

  vfs_node_t *base;
  if (dirfd == AT_FDCWD) {
    base = vfs_root;
  } else if (dirfd >= 0 && dirfd < MAX_PROCESS_FILES &&
             current_process->fd_table[dirfd]) {
    base = current_process->fd_table[dirfd];
  } else {
    return -EBADF;
  }

  // Resolve relative to base
  vfs_node_t *parent = vfs_resolve_path_relative(base, pathname);

  // If it already exists, fail
  if (parent) {
    return -EEXIST;
  }

  // Create directory (use VFS mkdir)
  return mkdir_vfs(base, pathname, mode);
}

// ============================================================================
// sys_unlinkat - Remove file relative to directory fd
// ============================================================================

int sys_unlinkat(int dirfd, const char *pathname, int flags) {
  if (!pathname)
    return -EFAULT;

  vfs_node_t *base;
  if (dirfd == AT_FDCWD) {
    base = vfs_root;
  } else if (dirfd >= 0 && dirfd < MAX_PROCESS_FILES &&
             current_process->fd_table[dirfd]) {
    base = current_process->fd_table[dirfd];
  } else {
    return -EBADF;
  }

  if (flags & AT_REMOVEDIR) {
    return rmdir_vfs(base, pathname);
  } else {
    return unlink_vfs(base, pathname);
  }
}

// ============================================================================
// sys_renameat - Rename file relative to directory fds
// ============================================================================

int sys_renameat(int olddirfd, const char *oldpath, int newdirfd,
                 const char *newpath) {
  if (!oldpath || !newpath)
    return -EFAULT;

  // Simplified: only support same-directory renames
  vfs_node_t *old_base = (olddirfd == AT_FDCWD)
                             ? vfs_root
                             : ((olddirfd >= 0 && olddirfd < MAX_PROCESS_FILES)
                                    ? current_process->fd_table[olddirfd]
                                    : 0);
  vfs_node_t *new_base = (newdirfd == AT_FDCWD)
                             ? vfs_root
                             : ((newdirfd >= 0 && newdirfd < MAX_PROCESS_FILES)
                                    ? current_process->fd_table[newdirfd]
                                    : 0);

  if (!old_base || !new_base)
    return -EBADF;

  // Check source exists
  vfs_node_t *src = vfs_resolve_path_relative(old_base, oldpath);
  if (!src)
    return -ENOENT;

  // Check destination doesn't exist
  vfs_node_t *dst = vfs_resolve_path_relative(new_base, newpath);
  if (dst)
    return -EEXIST;

  // Rename in VFS (simplified: just update name)
  // Real implementation would need filesystem-level support
  strncpy(src->name, newpath, 127);
  src->name[127] = 0;

  return 0;
}

// ============================================================================
// sys_rename - Rename a file
// ============================================================================

int sys_rename(const char *oldpath, const char *newpath) {
  return sys_renameat(AT_FDCWD, oldpath, AT_FDCWD, newpath);
}

// ============================================================================
// sys_fchmod - Change file permissions via fd
// ============================================================================

int sys_fchmod(int fd, uint32_t mode) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  current_process->fd_table[fd]->mask = mode;
  return 0;
}

// ============================================================================
// sys_chown - Change file ownership
// ============================================================================

int sys_chown(const char *pathname, uint32_t owner, uint32_t group) {
  if (!pathname)
    return -EFAULT;

  vfs_node_t *node = vfs_resolve_path(pathname);
  if (!node)
    return -ENOENT;

  // In real POSIX, only root can chown
  node->uid = owner;
  node->gid = group;
  return 0;
}

// ============================================================================
// sys_fchown - Change ownership via fd
// ============================================================================

int sys_fchown(int fd, uint32_t owner, uint32_t group) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  current_process->fd_table[fd]->uid = owner;
  current_process->fd_table[fd]->gid = group;
  return 0;
}

// ============================================================================
// sys_fcntl - File control operations
// ============================================================================

// Per-fd flags storage (simplified)
static uint32_t fd_flags[256];

int sys_fcntl(int fd, int cmd, int arg) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  switch (cmd) {
  case F_DUPFD:
  case F_DUPFD_CLOEXEC: {
    // Find first available fd >= arg
    for (int i = arg; i < MAX_PROCESS_FILES; i++) {
      if (!current_process->fd_table[i]) {
        current_process->fd_table[i] = current_process->fd_table[fd];
        current_process->fd_table[i]->ref_count++;
        if (cmd == F_DUPFD_CLOEXEC)
          fd_flags[i] = FD_CLOEXEC;
        return i;
      }
    }
    return -EMFILE;
  }

  case F_GETFD:
    return fd_flags[fd];

  case F_SETFD:
    fd_flags[fd] = arg & FD_CLOEXEC;
    return 0;

  case F_GETFL:
    // Return open flags (simplified - we don't track these)
    return O_RDWR;

  case F_SETFL:
    // Set file status flags (simplified - O_APPEND, O_NONBLOCK, etc.)
    // Not fully implemented
    return 0;

  case F_GETOWN:
  case F_SETOWN:
    // SIGIO owner - not implemented
    return 0;

  default:
    return -EINVAL;
  }
}

// ============================================================================
// sys_ioctl - I/O control
// ============================================================================

// Common ioctl requests
#define TIOCGWINSZ 0x5413 // Get window size
#define TIOCSWINSZ 0x5414 // Set window size
#define FIONREAD 0x541B   // Bytes available to read
#define FIONBIO 0x5421    // Set/clear non-blocking I/O

int sys_ioctl(int fd, unsigned long request, void *argp) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  vfs_node_t *node = current_process->fd_table[fd];

  switch (request) {
  case TIOCGWINSZ: {
    // Return terminal window size
    if (argp) {
      uint16_t *ws = (uint16_t *)argp;
      ws[0] = 25; // rows
      ws[1] = 80; // cols
      ws[2] = 0;  // xpixel
      ws[3] = 0;  // ypixel
    }
    return 0;
  }

  case FIONREAD: {
    // Return bytes available
    if (argp) {
      *(int *)argp = node->length - fd_positions[fd];
    }
    return 0;
  }

  case FIONBIO: {
    // Set non-blocking - not fully implemented
    return 0;
  }

  default:
    // Unknown ioctl - might be handled by driver
    return -ENOTTY;
  }
}

// ============================================================================
// sys_fchdir - Change directory via file descriptor
// ============================================================================

int sys_fchdir(int fd) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  vfs_node_t *node = current_process->fd_table[fd];
  if ((node->flags & 0x7) != VFS_DIRECTORY)
    return -ENOTDIR;

  // Update current working directory to the node's path
  // Simplified: we'd need to reconstruct the path from the node
  // For now, just set to the node's name (incomplete)
  strcpy(current_process->cwd, "/");
  strcat(current_process->cwd, node->name);

  return 0;
}

// ============================================================================
// sys_access - Check file accessibility
// ============================================================================

int sys_access(const char *pathname, int mode) {
  if (!pathname)
    return -EFAULT;

  vfs_node_t *node = vfs_resolve_path(pathname);
  if (!node)
    return -ENOENT;

  // F_OK: file exists (already passed if we got here)
  if (mode == 0)
    return 0;

  // For now, always allow access (simplified)
  // Real implementation would check uid/gid and permissions
  return 0;
}

// ============================================================================
// sys_faccessat - Check file accessibility relative to dirfd
// ============================================================================

int sys_faccessat(int dirfd, const char *pathname, int mode, int flags) {
  if (!pathname)
    return -EFAULT;

  vfs_node_t *base;
  if (dirfd == AT_FDCWD) {
    base = vfs_root;
  } else if (dirfd >= 0 && dirfd < MAX_PROCESS_FILES &&
             current_process->fd_table[dirfd]) {
    base = current_process->fd_table[dirfd];
  } else {
    return -EBADF;
  }

  vfs_node_t *node = vfs_resolve_path_relative(base, pathname);
  if (!node)
    return -ENOENT;

  // Simplified access check
  return 0;
}

} // extern "C"
