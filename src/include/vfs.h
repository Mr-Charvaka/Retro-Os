#ifndef VFS_H
#define VFS_H

#include "types.h"

// Phase 1: Real VFS Node Types
enum vfs_node_type {
  VFS_FILE,
  VFS_DIRECTORY,
  VFS_SYMLINK,
  VFS_DEVICE,
  VFS_MOUNTPOINT,
  VFS_PIPE,
  VFS_SOCKET
};

// Open flags (Legacy Compatible)
#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR 0x02
#define O_APPEND 0x08
#define O_CREAT 0x40
#define O_TRUNC 0x200
#define O_NONBLOCK 0x800

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Forward Declarations
struct vfs_node;
struct filesystem;
struct dirent;

// Filesystem Interface (The Contract)
struct filesystem {
  const char *name;

  // Core Operations
  struct vfs_node *(*mount)(struct filesystem *fs, void *device);
  struct vfs_node *(*lookup)(struct vfs_node *dir, const char *name);
  int (*create)(struct vfs_node *parent, const char *name, int type);

  // File Operations
  int (*read)(struct vfs_node *file, uint64_t offset, void *buffer,
              uint64_t size);
  int (*write)(struct vfs_node *file, uint64_t offset, const void *buffer,
               uint64_t size);

  // Management
  void (*close)(struct vfs_node *node);
  struct dirent *(*readdir)(struct vfs_node *dir, uint32_t index);
  int (*mkdir)(struct vfs_node *parent, const char *name, uint32_t mode);
  int (*unlink)(struct vfs_node *parent, const char *name);
  int (*rmdir)(struct vfs_node *parent, const char *name);
  int (*rename)(struct vfs_node *parent, const char *old_name,
                const char *new_name);
};

// The VFS Node (The Brain)
typedef struct vfs_node {
  char name[256];
  enum vfs_node_type type; // vfs_node_type

  uint64_t inode;
  uint64_t size;
  uint32_t mask; // Permissions
  uint32_t uid;
  uint32_t gid;
  uint32_t flags;
  uint32_t ref_count;

  struct vfs_node *parent;
  struct vfs_node *children;     // For cache/RAMFS
  struct vfs_node *next_sibling; // Linked list

  struct filesystem *fs; // Which FS owns this node
  void *impl;            // Private driver data

  // Legacy / Override Pointers (For TTY, Sockets, etc.)
  uint32_t (*read)(struct vfs_node *, uint32_t, uint32_t, uint8_t *);
  uint32_t (*write)(struct vfs_node *, uint32_t, uint32_t, uint8_t *);
  void (*open)(struct vfs_node *);
  void (*close)(struct vfs_node *);
  struct dirent *(*readdir)(struct vfs_node *, uint32_t);
  struct vfs_node *(*finddir)(struct vfs_node *, const char *);
  int (*mkdir)(struct vfs_node *, const char *, uint32_t);
  int (*create)(struct vfs_node *, const char *, int); // Added for FAT16 legacy
  int (*unlink)(struct vfs_node *, const char *);
  int (*rmdir)(struct vfs_node *, const char *);
  int (*rename)(struct vfs_node *, const char *, const char *);
  int (*ioctl)(struct vfs_node *, int, void *);
} vfs_node_t;

#ifdef __cplusplus
extern "C" {
#endif

// Global Root
extern vfs_node_t *vfs_root;

// Core VFS API (The new implementation)
void vfs_init();
void vfs_mount(const char *path, struct filesystem *fs, void *device);
vfs_node_t *vfs_resolve_path(const char *path);
vfs_node_t *vfs_resolve_path_relative(vfs_node_t *base, const char *path);

// Operation Wrappers
int vfs_read(vfs_node_t *node, uint64_t offset, void *buf, uint64_t size);
int vfs_write(vfs_node_t *node, uint64_t offset, const void *buf,
              uint64_t size);
int vfs_create(const char *path, int type);
int vfs_unlink(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_mkdir(const char *path, uint32_t mode); // Helper
struct dirent *vfs_readdir(vfs_node_t *node, uint32_t index);
void vfs_close(vfs_node_t *node);

// Node-relative Helpers (Exported for file_ops.cpp & Kernel legacy)
int mkdir_vfs(vfs_node_t *node, const char *name, uint32_t mask);
int unlink_vfs(vfs_node_t *node, const char *name);
int rmdir_vfs(vfs_node_t *node, const char *name);
struct dirent *readdir_vfs(vfs_node_t *node, uint32_t index);
extern vfs_node_t *vfs_dev;

// Helpers
int vfs_is_dir(vfs_node_t *node);

// Phase A Bootstrap
void fs_phase_a_bootstrap();

#ifdef __cplusplus
}
#endif

#endif
