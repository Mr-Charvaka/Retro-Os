#ifndef KERNEL_VFS_PHASE4_H
#define KERNEL_VFS_PHASE4_H

#include "types.h"
#include "vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Full VFS integration logic for Phase 4
// This header can expose advanced VFS features if needed
// or just serve as the anchor for the phase 4 implementation file.

// High-level "smart" API for the GUI/Kernel
// These handle full path resolution + permissions + journaling automatically.

int v_open(const char *path, int flags);
int v_read(int fd, void *buf, int size);
int v_write(int fd, const void *buf, int size);
int v_close(int fd);
int v_list_dir(const char *path, char *buffer,
               int max_len); // Returns list of names
int v_mkdir(const char *path);
int v_delete(const char *path);

// Initializer for Phase 4 specific logic (if distinct from vfs_init)
void vfs_phase4_init();

#ifdef __cplusplus
}
#endif

#endif
