// ============================================================================
// dirent.cpp - POSIX Directory Stream Implementation
// Entirely hand-crafted for Retro-OS kernel
// ============================================================================

#include "../include/dirent.h"
#include "../drivers/serial.h"
#include "../include/errno.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "heap.h"
#include "memory.h"
#include "process.h"

extern "C" {

// Maximum number of open directory streams
#define MAX_DIR_STREAMS 32
static DIR dir_streams[MAX_DIR_STREAMS];
static int dir_streams_initialized = 0;

// ============================================================================
// Initialize directory stream pool
// ============================================================================
static void init_dir_streams(void) {
  if (dir_streams_initialized)
    return;
  for (int i = 0; i < MAX_DIR_STREAMS; i++) {
    dir_streams[i].node = 0;
    dir_streams[i].closed = 1;
  }
  dir_streams_initialized = 1;
}

// ============================================================================
// Allocate a directory stream
// ============================================================================
static DIR *alloc_dir_stream(void) {
  init_dir_streams();
  for (int i = 0; i < MAX_DIR_STREAMS; i++) {
    if (dir_streams[i].closed) {
      dir_streams[i].closed = 0;
      dir_streams[i].position = 0;
      dir_streams[i].fd = -1;
      return &dir_streams[i];
    }
  }
  return 0; // No available streams
}

// ============================================================================
// opendir - Open a directory stream
// ============================================================================
DIR *opendir(const char *name) {
  if (!name)
    return 0;

  vfs_node_t *node = vfs_resolve_path(name);
  if (!node)
    return 0;

  // Check if it's a directory
  if ((node->flags & 0x7) != VFS_DIRECTORY)
    return 0;

  DIR *dirp = alloc_dir_stream();
  if (!dirp)
    return 0;

  dirp->node = node;
  dirp->position = 0;
  dirp->fd = -1;

  serial_log("DIRENT: Opened directory stream");
  return dirp;
}

// ============================================================================
// fdopendir - Open directory stream from file descriptor
// ============================================================================
DIR *fdopendir(int fd) {
  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return 0;

  vfs_node_t *node = current_process->fd_table[fd];
  if ((node->flags & 0x7) != VFS_DIRECTORY)
    return 0;

  DIR *dirp = alloc_dir_stream();
  if (!dirp)
    return 0;

  dirp->node = node;
  dirp->position = 0;
  dirp->fd = fd;

  return dirp;
}

// ============================================================================
// readdir - Read next directory entry
// ============================================================================
struct dirent *readdir(DIR *dirp) {
  if (!dirp || dirp->closed || !dirp->node)
    return 0;

  if (!dirp->node->readdir)
    return 0;

  struct dirent *entry = dirp->node->readdir(dirp->node, dirp->position);
  if (!entry)
    return 0;

  // Copy to internal buffer
  dirp->entry.d_ino = entry->d_ino;
  dirp->entry.d_off = dirp->position;
  dirp->entry.d_reclen = sizeof(struct dirent);

  // Set d_type based on node type (if available)
  dirp->entry.d_type = DT_UNKNOWN;

  // Copy name
  strncpy(dirp->entry.d_name, entry->d_name, 255);
  dirp->entry.d_name[255] = 0;

  dirp->position++;

  return &dirp->entry;
}

// ============================================================================
// readdir_r - Reentrant version of readdir
// ============================================================================
int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result) {
  if (!dirp || !entry || !result)
    return -EINVAL;

  if (dirp->closed || !dirp->node) {
    *result = 0;
    return 0;
  }

  if (!dirp->node->readdir) {
    *result = 0;
    return 0;
  }

  struct dirent *vfs_entry = dirp->node->readdir(dirp->node, dirp->position);
  if (!vfs_entry) {
    *result = 0;
    return 0; // End of directory
  }

  entry->d_ino = vfs_entry->d_ino;
  entry->d_off = dirp->position;
  entry->d_reclen = sizeof(struct dirent);
  entry->d_type = DT_UNKNOWN;
  strncpy(entry->d_name, vfs_entry->d_name, 255);
  entry->d_name[255] = 0;

  dirp->position++;
  *result = entry;

  return 0;
}

// ============================================================================
// rewinddir - Reset directory stream position
// ============================================================================
void rewinddir(DIR *dirp) {
  if (!dirp || dirp->closed)
    return;
  dirp->position = 0;
}

// ============================================================================
// seekdir - Set directory stream position
// ============================================================================
void seekdir(DIR *dirp, long loc) {
  if (!dirp || dirp->closed)
    return;
  dirp->position = (uint32_t)loc;
}

// ============================================================================
// telldir - Get current directory stream position
// ============================================================================
long telldir(DIR *dirp) {
  if (!dirp || dirp->closed)
    return -1;
  return (long)dirp->position;
}

// ============================================================================
// closedir - Close directory stream
// ============================================================================
int closedir(DIR *dirp) {
  if (!dirp)
    return -EBADF;

  if (dirp->closed)
    return -EBADF;

  // Don't close the underlying fd if opened via fdopendir
  // (the caller owns the fd in that case)

  dirp->node = 0;
  dirp->closed = 1;
  dirp->position = 0;
  dirp->fd = -1;

  serial_log("DIRENT: Closed directory stream");
  return 0;
}

// ============================================================================
// dirfd - Get file descriptor associated with directory stream
// ============================================================================
int dirfd(DIR *dirp) {
  if (!dirp || dirp->closed)
    return -EBADF;

  // If opened via fdopendir, return that fd
  if (dirp->fd >= 0)
    return dirp->fd;

  // Otherwise, we'd need to allocate an fd for the node
  // For simplicity, return -1 (not available)
  return -1;
}

// ============================================================================
// sys_getdents - Get directory entries (Linux-style syscall)
// ============================================================================
struct linux_dirent {
  unsigned long d_ino;     // Inode number
  unsigned long d_off;     // Offset to next entry
  unsigned short d_reclen; // Length of this entry
  char d_name[1];          // Filename (variable length)
};

int sys_getdents(int fd, void *dirp, unsigned int count) {
  if (!dirp)
    return -EFAULT;

  if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd])
    return -EBADF;

  vfs_node_t *node = current_process->fd_table[fd];
  if ((node->flags & 0x7) != VFS_DIRECTORY)
    return -ENOTDIR;

  if (!node->readdir)
    return -ENOSYS;

  // We need to track the position per-fd
  // Using a simplified approach with a static position tracker
  static uint32_t getdents_pos[MAX_PROCESS_FILES] = {0};

  uint8_t *buf = (uint8_t *)dirp;
  unsigned int bytes_written = 0;
  uint32_t pos = getdents_pos[fd];

  while (bytes_written < count) {
    struct dirent *entry = node->readdir(node, pos);
    if (!entry)
      break; // End of directory

    // Calculate entry size
    size_t namelen = strlen(entry->d_name);
    size_t reclen = sizeof(struct linux_dirent) + namelen + 1;
    reclen = (reclen + 3) & ~3; // Align to 4 bytes

    if (bytes_written + reclen > count)
      break; // Buffer full

    // Fill in the entry
    struct linux_dirent *ld = (struct linux_dirent *)(buf + bytes_written);
    ld->d_ino = entry->d_ino;
    ld->d_off = pos + 1;
    ld->d_reclen = reclen;
    strcpy(ld->d_name, entry->d_name);

    bytes_written += reclen;
    pos++;
  }

  getdents_pos[fd] = pos;
  return bytes_written;
}

// ============================================================================
// scandir - Scan a directory (simplified implementation)
// ============================================================================
int scandir(const char *dirpath, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **)) {
  (void)compar; // Sorting not implemented

  if (!dirpath || !namelist)
    return -1;

  DIR *dirp = opendir(dirpath);
  if (!dirp)
    return -1;

  // First pass: count entries
  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(dirp)) != 0) {
    if (!filter || filter(entry))
      count++;
  }

  if (count == 0) {
    closedir(dirp);
    *namelist = 0;
    return 0;
  }

  // Allocate array
  struct dirent **list =
      (struct dirent **)kmalloc(count * sizeof(struct dirent *));
  if (!list) {
    closedir(dirp);
    return -1;
  }

  // Second pass: copy entries
  rewinddir(dirp);
  int i = 0;
  while ((entry = readdir(dirp)) != 0 && i < count) {
    if (!filter || filter(entry)) {
      list[i] = (struct dirent *)kmalloc(sizeof(struct dirent));
      if (list[i]) {
        *list[i] = *entry;
        i++;
      }
    }
  }

  closedir(dirp);
  *namelist = list;
  return i;
}

// ============================================================================
// alphasort - Compare directory entries alphabetically
// ============================================================================
int alphasort(const struct dirent **a, const struct dirent **b) {
  return strcmp((*a)->d_name, (*b)->d_name);
}

} // extern "C"
