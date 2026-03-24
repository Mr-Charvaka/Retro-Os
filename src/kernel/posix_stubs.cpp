// ============================================================================
// posix_stubs.cpp - Stub implementations for missing POSIX functions
// ============================================================================

#include "../drivers/serial.h"
#include "../include/errno.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "process.h"

extern "C" {

// ============================================================================
// File Operations Stubs
// ============================================================================

int pread(int fd, void *buf, size_t count, int offset) {
  (void)fd;
  (void)buf;
  (void)count;
  (void)offset;
  serial_log("pread: stub called");
  return -ENOSYS;
}

int pwrite(int fd, const void *buf, size_t count, int offset) {
  (void)fd;
  (void)buf;
  (void)count;
  (void)offset;
  serial_log("pwrite: stub called");
  return -ENOSYS;
}

int lseek(int fd, int offset, int whence) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  file_description_t *desc = current_process->fd_table[fd];
  if (whence == 0)      desc->offset = offset;
  else if (whence == 1) desc->offset += offset;
  else if (whence == 2) desc->offset = desc->node->size + offset;
  else return -EINVAL;

  return (int)desc->offset;
}

int rename(const char *oldpath, const char *newpath) {
  return vfs_rename(oldpath, newpath);
}

int getdents(int fd, void *dirp, unsigned int count) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  file_description_t *desc = current_process->fd_table[fd];
  if (!desc->node || desc->node->type != VFS_DIRECTORY)
    return -ENOTDIR;

  struct dirent *user_de = (struct dirent *)dirp;
  int read_count = 0;
  int bytes_written = 0;

  // Since Retro-OS dirent is fixed size, we just copy them one by one
  while (bytes_written + (int)sizeof(struct dirent) <= (int)count) {
    struct dirent *kernel_de = vfs_readdir(desc->node, (uint32_t)desc->offset);
    if (!kernel_de) break;

    memcpy(&user_de[read_count], kernel_de, sizeof(struct dirent));
    user_de[read_count].d_reclen = sizeof(struct dirent);
    
    desc->offset++;
    read_count++;
    bytes_written += sizeof(struct dirent);
  }

  return bytes_written;
}

int fchmod(int fd, int mode) {
  (void)fd;
  (void)mode;
  serial_log("fchmod: stub called");
  return -ENOSYS;
}

int chown(const char *pathname, int owner, int group) {
  (void)pathname;
  (void)owner;
  (void)group;
  serial_log("chown: stub called");
  return -ENOSYS;
}

int fchown(int fd, int owner, int group) {
  (void)fd;
  (void)owner;
  (void)group;
  serial_log("fchown: stub called");
  return -ENOSYS;
}

int fcntl(int fd, int cmd, int arg) {
  (void)fd;
  (void)cmd;
  (void)arg;
  serial_log("fcntl: stub called");
  return -ENOSYS;
}

// ============================================================================
// Directory Operations Stubs
// ============================================================================

int fchdir(int fd) {
  (void)fd;
  serial_log("fchdir: stub called");
  return -ENOSYS;
}

// ============================================================================
// Socket Operations Stubs
// ============================================================================

int listen(int sockfd, int backlog) {
  (void)sockfd;
  (void)backlog;
  serial_log("listen: stub called");
  return -ENOSYS;
}

int send(int sockfd, const void *buf, size_t len, int flags) {
  (void)sockfd;
  (void)buf;
  (void)len;
  (void)flags;
  serial_log("send: stub called");
  return -ENOSYS;
}

int recv(int sockfd, void *buf, size_t len, int flags) {
  (void)sockfd;
  (void)buf;
  (void)len;
  (void)flags;
  serial_log("recv: stub called");
  return -ENOSYS;
}

int sendto(int sockfd, const void *buf, size_t len, int flags,
           const void *dest_addr, unsigned int addrlen) {
  (void)sockfd;
  (void)buf;
  (void)len;
  (void)flags;
  (void)dest_addr;
  (void)addrlen;
  serial_log("sendto: stub called");
  return -ENOSYS;
}

int recvfrom(int sockfd, void *buf, size_t len, int flags, void *src_addr,
             unsigned int *addrlen) {
  (void)sockfd;
  (void)buf;
  (void)len;
  (void)flags;
  (void)src_addr;
  (void)addrlen;
  serial_log("recvfrom: stub called");
  return -ENOSYS;
}

int getsockname(int sockfd, void *addr, unsigned int *addrlen) {
  (void)sockfd;
  (void)addr;
  (void)addrlen;
  serial_log("getsockname: stub called");
  return -ENOSYS;
}

int getpeername(int sockfd, void *addr, unsigned int *addrlen) {
  (void)sockfd;
  (void)addr;
  (void)addrlen;
  serial_log("getpeername: stub called");
  return -ENOSYS;
}

int setsockopt(int sockfd, int level, int optname, const void *optval,
               unsigned int optlen) {
  (void)sockfd;
  (void)level;
  (void)optname;
  (void)optval;
  (void)optlen;
  serial_log("setsockopt: stub called");
  return -ENOSYS;
}

int getsockopt(int sockfd, int level, int optname, void *optval,
               unsigned int *optlen) {
  (void)sockfd;
  (void)level;
  (void)optname;
  (void)optval;
  (void)optlen;
  serial_log("getsockopt: stub called");
  return -ENOSYS;
}

int shutdown(int sockfd, int how) {
  (void)sockfd;
  (void)how;
  serial_log("shutdown: stub called");
  return -ENOSYS;
}

} // extern "C"
