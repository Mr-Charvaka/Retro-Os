#include "../include/kernel_fs.h"
#include "../drivers/serial.h"
#include "../include/errno.h"
#include "../include/kernel_fs_internal.h"
#include "../include/kernel_fs_phase3.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"

extern "C" {

/* ==========================================================
   DISK LAYER (ABSTRACT -> RAM FOR NOW)
   ========================================================== */

uint8_t *disk_memory = 0;
bool block_bitmap[MAX_BLOCKS];

// Helper to get block pointer
uint8_t *get_block_ptr(uint32_t block_idx) {
  if (block_idx >= MAX_BLOCKS)
    return 0;
  return disk_memory + (block_idx * BLOCK_SIZE);
}

/* ==========================================================
   DATA STRUCTURES (Now in header)
   ========================================================== */

struct inode inode_table[MAX_INODES];
struct superblock sb;

uint32_t recycle_bin_inode;

/* ==========================================================
   ALLOCATORS
   ========================================================== */

int alloc_block() {
  for (int i = 0; i < MAX_BLOCKS; i++) {
    if (!block_bitmap[i]) {
      block_bitmap[i] = true;
      sb.used_blocks++;
      memset(get_block_ptr(i), 0, BLOCK_SIZE);
      return i;
    }
  }
  serial_log("KERNEL_FS: Out of blocks!");
  return -1;
}

void free_block(int block) {
  if (block >= 0 && block < MAX_BLOCKS) {
    block_bitmap[block] = false;
    sb.used_blocks--;
  }
}

int alloc_inode(enum inode_type type, uint32_t parent) {
  for (int i = 0; i < MAX_INODES; i++) {
    if (!inode_table[i].used) {
      inode_table[i].used = true;
      inode_table[i].id = i;
      inode_table[i].type = type;
      inode_table[i].size = 0;
      inode_table[i].block_count = 0;
      inode_table[i].parent = parent;
      inode_table[i].permissions = 0755; // Default permissions
      inode_table[i].uid = 0;
      inode_table[i].gid = 0;
      sb.used_inodes++;
      return i;
    }
  }
  serial_log("KERNEL_FS: Out of inodes!");
  return -1;
}

/* ==========================================================
   INTERNAL FS OPERATIONS
   ========================================================== */

void dir_add(uint32_t dir_inode, uint32_t child_inode, const char *name) {
  struct inode *dir = &inode_table[dir_inode];

  // Allocate first block if needed
  if (dir->block_count == 0) {
    int b = alloc_block();
    if (b < 0)
      return;
    dir->blocks[0] = b;
    dir->block_count = 1;
  }

  struct dir_entry *entries = (struct dir_entry *)get_block_ptr(dir->blocks[0]);
  int max_entries = BLOCK_SIZE / sizeof(struct dir_entry);

  for (int i = 0; i < max_entries; i++) {
    if (entries[i].inode == 0) { // Empty slot
      entries[i].inode = child_inode;
      strncpy(entries[i].name, name, MAX_FILENAME - 1);
      entries[i].name[MAX_FILENAME - 1] = 0;
      dir->size += sizeof(struct dir_entry);
      return;
    }
  }
  serial_log("KERNEL_FS: Directory block full (Expansion not impl)");
}

int dir_lookup_internal(uint32_t dir_inode, const char *name) {
  struct inode *dir = &inode_table[dir_inode];
  if (dir->type != INODE_DIR || dir->block_count == 0)
    return -1;

  struct dir_entry *entries = (struct dir_entry *)get_block_ptr(dir->blocks[0]);
  int max_entries = BLOCK_SIZE / sizeof(struct dir_entry);

  for (int i = 0; i < max_entries; i++) {
    if (entries[i].inode != 0 && strcmp(entries[i].name, name) == 0) {
      return entries[i].inode;
    }
  }
  return -1;
}

int fs_create_internal(uint32_t parent_inode, const char *name,
                       enum inode_type type) {
  int inode = alloc_inode(type, parent_inode);
  if (inode < 0)
    return -1;

  dir_add(parent_inode, inode, name);
  return inode;
}

int fs_write_internal(uint32_t inode_id, const void *buffer, uint32_t size) {
  struct inode *node = &inode_table[inode_id];
  uint32_t written = 0;
  const uint8_t *buf = (const uint8_t *)buffer;

  // Overwrite support logic
  // For simplicity, we append or overwrite from 0?
  // The prompt's logic implies appending via alloc_block loop.
  // Let's implement full write support including offset in VFS wrapper.
  // This function writes FROM START (prompt style).

  // Reset for now to match prompt logic (Append modeish)
  // Real FS needs offset.

  // Let's adapt to support writing at offset in VFS wrapper,
  // but the core function provided in prompt was simple.
  // We'll reimplement specific block logic here.

  uint32_t current_offset = 0;

  // Simplification: Write fills blocks sequentially from 0
  node->block_count = 0; // Reset (Overwrite)
  node->size = 0;

  while (written < size && node->block_count < DIRECT_BLOCKS) {
    int block = alloc_block();
    if (block < 0)
      break;

    node->blocks[node->block_count++] = block;

    uint32_t to_copy =
        (size - written > BLOCK_SIZE) ? BLOCK_SIZE : size - written;

    memcpy(get_block_ptr(block), buf + written, to_copy);
    written += to_copy;
  }

  node->size = written;
  return written;
}

int fs_read_internal(uint32_t inode_id, void *buffer, uint32_t len,
                     uint32_t offset) {
  struct inode *node = &inode_table[inode_id];
  if (offset >= node->size)
    return 0;
  if (offset + len > node->size)
    len = node->size - offset;

  uint32_t read = 0;
  uint32_t current_pos = offset;
  uint8_t *buf = (uint8_t *)buffer;

  while (read < len) {
    uint32_t block_idx = current_pos / BLOCK_SIZE;
    uint32_t block_offset = current_pos % BLOCK_SIZE;

    if (block_idx >= node->block_count)
      break;

    uint32_t valid_in_block = BLOCK_SIZE - block_offset;
    uint32_t to_read =
        (len - read < valid_in_block) ? (len - read) : valid_in_block;

    memcpy(buf + read, get_block_ptr(node->blocks[block_idx]) + block_offset,
           to_read);

    read += to_read;
    current_pos += to_read;
  }

  return read;
}

/* ==========================================================
   VFS CONTRACT IMPLEMENTATION
   ========================================================== */

vfs_node_t *kfs_mount_wrapper(struct filesystem *fs, void *device) {
  // Return root node (inode 0)
  vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(node, 0, sizeof(vfs_node_t));
  strcpy(node->name, "kfs_root");
  node->inode = 0; // Root inode
  node->fs = fs;
  node->type = VFS_DIRECTORY;
  node->flags = VFS_DIRECTORY; // Legacy flag
  return node;
}

vfs_node_t *kfs_lookup_wrapper(vfs_node_t *dir, const char *name) {
  int inode = dir_lookup_internal(dir->inode, name);
  if (inode < 0)
    return 0; // Not found

  vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(node, 0, sizeof(vfs_node_t));
  strncpy(node->name, name, 255);
  node->inode = inode;
  node->fs = dir->fs;

  struct inode *in = &inode_table[inode];
  if (in->type == INODE_DIR) {
    node->type = VFS_DIRECTORY;
    node->flags = VFS_DIRECTORY;
  } else {
    node->type = VFS_FILE;
    node->flags = VFS_FILE;
    node->size = in->size;
  }
  return node;
}

int kfs_create_wrapper(vfs_node_t *parent, const char *name, int type) {
  enum inode_type t = (type == VFS_DIRECTORY) ? INODE_DIR : INODE_FILE;
  int inode = fs_create_secure(parent->inode, name, t, 0755);
  return (inode >= 0) ? 0 : -1;
}

int kfs_read_wrapper(vfs_node_t *file, uint64_t offset, void *buffer,
                     uint64_t size) {
  return fs_read_internal(file->inode, buffer, (uint32_t)size,
                          (uint32_t)offset);
}

int kfs_write_wrapper(vfs_node_t *file, uint64_t offset, const void *buffer,
                      uint64_t size) {
  // Use Secure Write (Phase 3)
  // Writes are journaled and permission checked
  return fs_write_secure(file->inode, buffer, (uint32_t)size);
}

struct dirent kfs_dirent;
struct dirent *kfs_readdir_wrapper(vfs_node_t *dir, uint32_t index) {
  struct inode *in = &inode_table[dir->inode];
  if (in->block_count == 0)
    return 0;

  struct dir_entry *entries = (struct dir_entry *)get_block_ptr(in->blocks[0]);
  int max_entries = BLOCK_SIZE / sizeof(struct dir_entry);

  // We need to skip empty entries to find the Nth valid entry
  int valid_count = 0;
  for (int i = 0; i < max_entries; i++) {
    if (entries[i].inode != 0) {
      if (valid_count == (int)index) {
        kfs_dirent.d_ino = entries[i].inode;
        strncpy(kfs_dirent.d_name, entries[i].name, 255);
        kfs_dirent.d_type =
            (inode_table[entries[i].inode].type == INODE_DIR) ? 0x02 : 0x01;
        return &kfs_dirent;
      }
      valid_count++;
    }
  }
  return 0;
}

/* ==========================================================
   DRIVER EXPORT
   ========================================================== */

struct filesystem kernel_fs = {
    .name = "kernel_fs",
    .mount = kfs_mount_wrapper,
    .lookup = kfs_lookup_wrapper,
    .create = kfs_create_wrapper,
    .read = kfs_read_wrapper,
    .write = kfs_write_wrapper,
    .close = 0, // Default cleanup
    .readdir = kfs_readdir_wrapper,
    .mkdir = 0, // Should map to create
    .unlink = 0 // Not impl yet
};

/* ==========================================================
   INITIALIZATION
   ========================================================== */

// Global Legacy Wrappers for Unified Kernel
uint32_t fs_create_file() {
  return alloc_inode(
      INODE_FILE, 0); // Create orphan file, will be linked later or just exist
}

int fs_write(uint32_t inode_idx, const void *data, uint32_t size) {
  if (inode_idx >= MAX_INODES || !inode_table[inode_idx].used)
    return -1;
  struct inode *node = &inode_table[inode_idx];

  // Simple single block write for legacy support
  if (node->block_count == 0) {
    int b = alloc_block();
    if (b < 0)
      return -1;
    node->blocks[0] = b;
    node->block_count = 1;
  }

  uint8_t *ptr = get_block_ptr(node->blocks[0]);
  if (size > BLOCK_SIZE)
    size = BLOCK_SIZE;
  memcpy(ptr, data, size);
  node->size = size;
  return size;
}

// GUI Disk Stats Wrapper
int sys_disk_stats(uint64_t *total, uint64_t *free) {
  if (!total || !free)
    return -1;
  *total = (uint64_t)sb.total_blocks * BLOCK_SIZE;
  *free = (uint64_t)(sb.total_blocks - sb.used_blocks) * BLOCK_SIZE;
  return 0;
}

void kernel_fs_init() {
  // 1. Allocate Disk Memory
  // 4096 blocks * 4096 bytes = 16MB
  uint32_t disk_size = MAX_BLOCKS * BLOCK_SIZE;
  disk_memory = (uint8_t *)kmalloc(disk_size);
  // Assuming kmalloc_aligned exists or normal kmalloc is aligned enough.
  // Fallback to kmalloc if aligned not available, usually 8-byte aligned. 4k
  // needed? No, just pointer.
  // if (!disk_memory) {
  //  disk_memory = (uint8_t *)kmalloc(disk_size);
  // }

  if (!disk_memory) {
    serial_log("KERNEL_FS: Critical Failure - Could not allocate 16MB disk!");
    return;
  }

  memset(disk_memory, 0, disk_size);
  memset(block_bitmap, 0, sizeof(block_bitmap));
  memset(inode_table, 0, sizeof(inode_table));

  sb.total_blocks = MAX_BLOCKS;
  sb.total_inodes = MAX_INODES;
  sb.used_blocks = 0;
  sb.used_inodes = 0;

  // 2. Create Root Structure (Phase 2 Req)
  int root = alloc_inode(INODE_DIR, 0); // Inode 0 is root

  // Create standard folders inside the disk
  int home = fs_create_internal(root, "home", INODE_DIR);
  int user = fs_create_internal(home, "user", INODE_DIR);
  fs_create_internal(user, "Desktop", INODE_DIR);
  fs_create_internal(user, "Downloads", INODE_DIR);

  int recycle_bin_inode = fs_create_internal(user, ".recycle_bin", INODE_DIR);
  (void)recycle_bin_inode;

  serial_log("KERNEL_FS: Initialized with structure /home/user/Desktop");
}

} // extern "C"
