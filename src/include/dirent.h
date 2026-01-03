#ifndef DIRENT_H
#define DIRENT_H

#include "types.h"
#include "vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Directory Entry Structure
// ============================================================================
// Already defined in vfs.h, but we provide additional compatibility here

// ============================================================================
// Directory Stream Structure
// ============================================================================
typedef struct DIR {
  vfs_node_t *node;    // The directory node
  uint32_t position;   // Current entry index
  struct dirent entry; // Current entry buffer
  int fd;              // File descriptor (if opened via fdopendir)
  int closed;          // Stream closed flag
} DIR;

// ============================================================================
// Directory Type Constants (for d_type)
// ============================================================================
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

// ============================================================================
// IFTODT and DTTOIF macros
// ============================================================================
#define IFTODT(mode) (((mode) & 0170000) >> 12)
#define DTTOIF(type) ((type) << 12)

// ============================================================================
// Directory Stream Functions
// ============================================================================
DIR *opendir(const char *name);
DIR *fdopendir(int fd);
struct dirent *readdir(DIR *dirp);
int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result);
void rewinddir(DIR *dirp);
void seekdir(DIR *dirp, long loc);
long telldir(DIR *dirp);
int closedir(DIR *dirp);
int dirfd(DIR *dirp);

// ============================================================================
// Kernel-level syscalls
// ============================================================================
int sys_getdents(int fd, void *dirp, unsigned int count);

#ifdef __cplusplus
}
#endif

#endif // DIRENT_H
