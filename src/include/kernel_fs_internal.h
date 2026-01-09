#ifndef KERNEL_FS_INTERNAL_H
#define KERNEL_FS_INTERNAL_H

#include "kernel_fs.h" // For struct filesystem
#include "types.h"

#define MAX_INODES 4096
#define MAX_BLOCKS 4096
#define BLOCK_SIZE 4096
#define MAX_FILENAME 128
#define DIRECT_BLOCKS 12

enum inode_type { INODE_FILE, INODE_DIR };

struct inode {
  uint32_t id;
  enum inode_type type;
  uint32_t size;
  uint32_t blocks[DIRECT_BLOCKS];
  uint32_t block_count;
  uint32_t parent;
  bool used;
  // Phase 3 Extensions can be added here or in parallel array
  uint32_t uid;
  uint32_t gid;
  uint32_t permissions; // rwx rwx rwx
};

struct dir_entry {
  uint32_t inode;
  char name[MAX_FILENAME];
};

struct superblock {
  uint32_t total_blocks;
  uint32_t used_blocks;
  uint32_t total_inodes;
  uint32_t used_inodes;
};

#ifdef __cplusplus
extern "C" {
#endif

// Externals shared between kernel_fs.cpp and kernel_fs_phase3.cpp
extern uint8_t *disk_memory;
extern bool block_bitmap[MAX_BLOCKS];
extern struct inode inode_table[MAX_INODES];
extern struct superblock sb;

// Core internal functions
uint8_t *get_block_ptr(uint32_t block_idx);
int alloc_block();
void free_block(int block);
int alloc_inode(enum inode_type type, uint32_t parent);
void dir_add(uint32_t dir_inode, uint32_t child_inode, const char *name);
int dir_lookup_internal(uint32_t dir_inode, const char *name);
int fs_create_internal(uint32_t parent_inode, const char *name,
                       enum inode_type type);
int fs_write_internal(uint32_t inode_id, const void *buffer, uint32_t size);
int fs_read_internal(uint32_t inode_id, void *buffer, uint32_t len,
                     uint32_t offset);

#ifdef __cplusplus
}
#endif

#endif
