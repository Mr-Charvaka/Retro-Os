#include "../include/kernel_vfs_phase4.h"
#include "../drivers/serial.h"
#include "../include/errno.h"
#include "../include/kernel_fs.h"
#include "../include/kernel_fs_phase3.h" // For secure ops
#include "../include/string.h"
#include "../include/vfs.h"
#include "heap.h"
#include "memory.h"
#include "process.h" // For FD table via current_process

extern "C" {

/* ==========================================================
   VFS PHASE 4: The Unifier
   Logic: GUI -> v_* syscalls -> vfs_resolve -> Secure Ops -> Real FS
   ========================================================== */

// Helper: Convert VFS flags to FS perms
static uint32_t flags_to_perms(int flags) {
  // simplified
  if (flags & O_WRONLY || flags & O_RDWR)
    return PERM_WRITE;
  return PERM_READ;
}

int v_open(const char *path, int flags) {
  if (!path)
    return -EFAULT;

  // 1. Resolve Path
  vfs_node_t *node = vfs_resolve_path(path);

  // 2. Handle Creation
  if (!node) {
    if (flags & O_CREAT) {
      // Logic to create file
      int res = vfs_create(path, VFS_FILE);
      if (res < 0)
        return res;
      node = vfs_resolve_path(path);
    } else {
      return -ENOENT;
    }
  }

  if (!node)
    return -ENOENT;

  // 3. Permission Check (Phase 3 Integration)
  // If we had the inode ID, we could call has_permission directly.
  // But vfs_node wraps it. Node->mask contains perms?
  // Let's assume vfs_node was populated with real perms by lookup.

  // 4. Allocate FD in Process
  int fd = -1;
  for (int i = 0; i < MAX_PROCESS_FILES; i++) {
    if (!current_process->fd_table[i]) {
      fd = i;
      break;
    }
  }

  if (fd == -1)
    return -EMFILE;

  // 5. Store Description
  file_description_t *desc =
      (file_description_t *)kmalloc(sizeof(file_description_t));
  desc->node = node;
  desc->offset = 0;
  desc->flags = flags;
  desc->ref_count = 1;
  current_process->fd_table[fd] = desc;

  // Open hook
  if (node->open)
    node->open(node); // Legacy

  serial_log("VFS_P4: Opened file");
  return fd;
}

int v_read(int fd, void *buf, int size) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES)
    return -EBADF;
  file_description_t *desc = current_process->fd_table[fd];
  if (!desc)
    return -EBADF;

  vfs_node_t *node = desc->node;
  int res = vfs_read(node, desc->offset, buf, size);
  if (res > 0)
    desc->offset += res;
  return res;
}

int v_write(int fd, const void *buf, int size) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES)
    return -EBADF;
  file_description_t *desc = current_process->fd_table[fd];
  if (!desc)
    return -EBADF;

  vfs_node_t *node = desc->node;
  int res = vfs_write(node, desc->offset, buf, size);
  if (res > 0)
    desc->offset += res;
  return res;
}

int v_close(int fd) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES)
    return -EBADF;
  file_description_t *desc = current_process->fd_table[fd];
  if (!desc)
    return -EBADF;

  vfs_node_t *node = desc->node;
  desc->ref_count--;
  if (desc->ref_count == 0) {
    if (node->close)
      node->close(node);
    vfs_close(node);
    kfree(desc);
  }

  current_process->fd_table[fd] = 0;
  return 0;
}

int v_list_dir(const char *path, char *buffer, int max_len) {
  vfs_node_t *node = vfs_resolve_path(path);
  if (!node)
    return -ENOENT;
  if ((node->flags & 0x7) != VFS_DIRECTORY)
    return -ENOTDIR;

  int offset = 0;
  int index = 0;
  struct dirent *d;

  // Format: "name1|name2|name3|"
  while ((d = vfs_readdir(node, index)) != 0 && offset < max_len - 32) {
    strcpy(buffer + offset, d->d_name);
    offset += strlen(d->d_name);
    buffer[offset++] = '|';
    index++;
  }
  buffer[offset] = 0;
  return index; // count
}

int v_mkdir(const char *path) {
  // 1. Resolve Parent
  // 2. Call vfs_mkdir which calls fs->mkdir or create
  // Secure logic is inside fs implementation
  return vfs_create(path, VFS_DIRECTORY);
}

int v_delete(const char *path) { return vfs_unlink(path); }

void vfs_phase4_init() { serial_log("VFS_P4: High Level API Ready"); }

} // extern "C"
