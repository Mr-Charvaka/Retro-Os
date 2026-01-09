#ifndef KERNEL_FS_H
#define KERNEL_FS_H

#include "types.h"
#include "vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the Kernel Filesystem (allocates memory, sets up superblocks)
void kernel_fs_init();

// The filesystem driver structure to register with VFS
extern struct filesystem kernel_fs;

#ifdef __cplusplus
}
#endif

#endif
