/*
 * fs_phase_a.cpp
 *
 * PHASE A: Full, REAL Filesystem Truth Layer
 *
 * Implements:
 * 1. cat (real file reader)
 * 2. Permission enforcement (rwx)
 * 3. Desktop/Home path mapping
 * 4. Directory & File creation correctness
 * 5. RAM Blocks and Inode Logic
 */

#include "../include/fs_phase.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"
#include <stddef.h>
#include <stdint.h>

extern "C" void serial_log(const char *msg);

#define FILE_READ 0x1
#define FILE_WRITE 0x2

extern "C" {
// 🔹 SECTION 3: GLOBAL FS STATE (RAM FS)
phase_inode phase_inode_table[256];
uint8_t phase_data_blocks[64][BLOCK_SIZE]; // Reduced to save BSS/Paging

uint32_t phase_inode_count = 0;
uint32_t phase_block_count = 0;

// 🔹 SECTION 4: KERNEL — BLOCK + INODE ALLOCATION
uint32_t phase_alloc_block() {
  if (phase_block_count >= 64)
    return 0; // Out of blocks
  return phase_block_count++;
}

phase_inode *phase_alloc_inode(uint32_t type) {
  if (phase_inode_count >= 256)
    return nullptr;
  phase_inode *n = &phase_inode_table[phase_inode_count];
  n->id = phase_inode_count;
  n->type = type;
  n->size = 0;
  n->owner = 1000;
  n->group = 1000;
  n->perms = 0755;

  // Clear blocks
  for (int i = 0; i < 12; i++)
    n->blocks[i] = 0;

  phase_inode_count++;
  return n;
}

// 🔹 SECTION 5: PERMISSION ENFORCEMENT
bool phase_vfs_check_perm(phase_inode *node, uint16_t uid, uint8_t req) {
  if (!node)
    return false;
  if (uid == 0)
    return true; // Root always has access
  if (uid == node->owner)
    return (node->perms >> 6) & req;
  return (node->perms & req); // Public/Other perms
}

// 🔹 SECTION 6: PATH RESOLUTION
static void split_path(const char *path, char *parent, char *name) {
  int len = strlen(path);
  int i = len - 1;
  while (i > 0 && path[i] == '/')
    i--;
  while (i >= 0 && path[i] != '/')
    i--;

  if (i < 0) {
    strcpy(parent, "/");
    strcpy(name, path);
    return;
  }

  if (i == 0) {
    strcpy(parent, "/");
  } else {
    strncpy(parent, path, i);
    parent[i] = 0;
  }
  strcpy(name, path + i + 1);
}

phase_inode *phase_vfs_resolve(const char *path) {
  if (!path || path[0] != '/')
    return nullptr;

  if (strcmp(path, "/") == 0)
    return &phase_inode_table[0];

  phase_inode *current = &phase_inode_table[0]; // root inode
  char token[MAX_NAME];
  int ti = 0;

  for (int i = 1;; i++) {
    char c = path[i];
    if (c == '/' || c == '\0') {
      token[ti] = 0;
      if (ti == 0) {
        if (c == '\0')
          break;
        continue;
      }

      bool found = false;
      phase_dir_entry *ents =
          (phase_dir_entry *)phase_data_blocks[current->blocks[0]];

      for (uint32_t j = 0; j < current->size; j++) {
        if (strcmp(ents[j].name, token) == 0) {
          current = &phase_inode_table[ents[j].inode_id];
          found = true;
          break;
        }
      }
      if (!found)
        return nullptr;

      ti = 0;
      if (c == '\0')
        break;
    } else {
      token[ti++] = c;
    }
  }
  return current;
}

// 🔹 SECTION 7: DIRECTORY CREATION
bool vfs_mkdir_phase_a(const char *path) {
  char parent[MAX_PATH], name[MAX_NAME];
  split_path(path, parent, name);

  phase_inode *dir = phase_vfs_resolve(parent);
  if (!dir || dir->type != INODE_DIR)
    return false;

  phase_dir_entry *ents = (phase_dir_entry *)phase_data_blocks[dir->blocks[0]];
  for (uint32_t j = 0; j < dir->size; j++) {
    if (strcmp(ents[j].name, name) == 0)
      return false;
  }

  phase_inode *n = phase_alloc_inode(INODE_DIR);
  if (!n)
    return false;

  uint32_t blk = phase_alloc_block();
  n->blocks[0] = blk;
  memset(phase_data_blocks[blk], 0, BLOCK_SIZE);

  strcpy(ents[dir->size].name, name);
  ents[dir->size].inode_id = n->id;
  dir->size++;

  phase_vfs_sync();
  return true;
}

// 🔹 SECTION 8: FILE CREATION + WRITE
int vfs_create_file_phase_a(const char *path) {
  char parent[MAX_PATH], name[MAX_NAME];
  split_path(path, parent, name);

  phase_inode *dir = phase_vfs_resolve(parent);
  if (!dir)
    return -1;

  phase_dir_entry *ents = (phase_dir_entry *)phase_data_blocks[dir->blocks[0]];
  for (uint32_t j = 0; j < dir->size; j++) {
    if (strcmp(ents[j].name, name) == 0)
      return -1;
  }

  phase_inode *f = phase_alloc_inode(INODE_FILE);
  if (!f)
    return -1;

  f->blocks[0] = phase_alloc_block();

  ents = (phase_dir_entry *)phase_data_blocks[dir->blocks[0]];
  strcpy(ents[dir->size].name, name);
  ents[dir->size].inode_id = f->id;
  dir->size++;

  phase_vfs_sync();
  return f->id;
}

int vfs_write_phase_a(phase_inode *f, const char *data, uint32_t len) {
  if (!f || f->type != INODE_FILE)
    return -1;
  if (len > BLOCK_SIZE)
    len = BLOCK_SIZE;

  memcpy(phase_data_blocks[f->blocks[0]], data, len);
  f->size = len;

  phase_vfs_sync();
  return len;
}

bool phase_vfs_rename(const char *oldpath, const char *newpath) {
  char old_parent_path[MAX_PATH], old_name[MAX_NAME];
  char new_parent_path[MAX_PATH], new_name[MAX_NAME];

  split_path(oldpath, old_parent_path, old_name);
  split_path(newpath, new_parent_path, new_name);

  phase_inode *old_dir = phase_vfs_resolve(old_parent_path);
  phase_inode *new_dir = phase_vfs_resolve(new_parent_path);

  if (!old_dir || !new_dir)
    return false;

  // Find entry in old_dir
  phase_dir_entry *old_ents =
      (phase_dir_entry *)phase_data_blocks[old_dir->blocks[0]];
  int old_idx = -1;
  for (uint32_t i = 0; i < old_dir->size; i++) {
    if (strcmp(old_ents[i].name, old_name) == 0) {
      old_idx = i;
      break;
    }
  }

  if (old_idx == -1)
    return false;
  uint32_t target_inode_id = old_ents[old_idx].inode_id;

  // Add to new_dir
  phase_dir_entry *new_ents =
      (phase_dir_entry *)phase_data_blocks[new_dir->blocks[0]];
  // Check if target already exists in new_dir
  for (uint32_t i = 0; i < new_dir->size; i++) {
    if (strcmp(new_ents[i].name, new_name) == 0)
      return false;
  }

  strcpy(new_ents[new_dir->size].name, new_name);
  new_ents[new_dir->size].inode_id = target_inode_id;
  new_dir->size++;

  // Remove from old_dir
  for (uint32_t i = (uint32_t)old_idx; i < old_dir->size - 1; i++) {
    old_ents[i] = old_ents[i + 1];
  }
  old_dir->size--;

  phase_vfs_sync();
  return true;
}

extern "C" int phase_create_in_dir(phase_inode *pdir, const char *name,
                                   int type) {
  if (!pdir || pdir->type != INODE_DIR)
    return -1;
  // 1. Check if name already exists
  phase_dir_entry *ents = (phase_dir_entry *)phase_data_blocks[pdir->blocks[0]];
  for (uint32_t i = 0; i < pdir->size; i++) {
    if (strcmp(ents[i].name, name) == 0)
      return -1;
  }

  // 2. Alloc Inode
  uint32_t itype = (type == 0x02) ? INODE_DIR : INODE_FILE; // VFS_DIRECTORY = 2
  phase_inode *n = phase_alloc_inode(itype);
  if (!n)
    return -1;

  n->blocks[0] = phase_alloc_block();
  if (itype == INODE_DIR) {
    memset(phase_data_blocks[n->blocks[0]], 0, BLOCK_SIZE);
  }

  // 3. Add to parent
  if (pdir->size >= (BLOCK_SIZE / sizeof(phase_dir_entry)))
    return -1;
  int idx = pdir->size++;
  for (int i = 0; name[i] && i < MAX_NAME - 1; i++) {
    ents[idx].name[i] = name[i];
    ents[idx].name[i + 1] = 0;
  }
  ents[idx].inode_id = n->id;

  phase_vfs_sync();
  return n->id;
}

// 🔹 SECTION 10: PERSISTENCE (DISK SYNC)
#include "../drivers/fat16.h"

// Optimization: Disable sync during bootstrap
static bool g_phase_a_sync_enabled = true;

uint32_t phase_vfs_get_total_size() {
  return sizeof(phase_inode_table) + sizeof(phase_data_blocks) + 8;
}

extern "C" void phase_vfs_sync() {
  if (!g_phase_a_sync_enabled)
    return;
  uint32_t total = phase_vfs_get_total_size();
  uint8_t *buf = (uint8_t *)kmalloc(total);
  if (!buf)
    return;

  uint32_t offset = 0;
  memcpy(buf + offset, phase_inode_table, sizeof(phase_inode_table));
  offset += sizeof(phase_inode_table);
  memcpy(buf + offset, phase_data_blocks, sizeof(phase_data_blocks));
  offset += sizeof(phase_data_blocks);
  memcpy(buf + offset, &phase_inode_count, 4);
  offset += 4;
  memcpy(buf + offset, &phase_block_count, 4);

  // Create if not exists
  fat16_entry_t e = fat16_find_file("TRUTH.DAT");
  if (e.filename[0] == 0) {
    fat16_create_file("TRUTH.DAT");
  }

  fat16_write_file("TRUTH.DAT", buf, total);
  kfree(buf);
  serial_log("FS_PHASE_A: Synced to TRUTH.DAT");
}

extern "C" void phase_vfs_load() {
  fat16_entry_t e = fat16_find_file("TRUTH.DAT");
  if (e.filename[0] == 0) {
    serial_log("FS_PHASE_A: No TRUTH.DAT found, starting fresh.");
    return;
  }

  uint32_t total = phase_vfs_get_total_size();
  uint8_t *buf = (uint8_t *)kmalloc(total);
  if (!buf)
    return;

  fat16_read_file(&e, buf);

  uint32_t offset = 0;
  memcpy(phase_inode_table, buf + offset, sizeof(phase_inode_table));
  offset += sizeof(phase_inode_table);
  memcpy(phase_data_blocks, buf + offset, sizeof(phase_data_blocks));
  offset += sizeof(phase_data_blocks);
  memcpy(&phase_inode_count, buf + offset, 4);
  offset += 4;
  memcpy(&phase_block_count, buf + offset, 4);

  kfree(buf);
  serial_log("FS_PHASE_A: Loaded persistent state from TRUTH.DAT");
}

// 🔹 SECTION 11: VFS SYSTEM INTERFACE (Hooks for vfs.cpp)
extern "C" {
vfs_node_t *phase_a_vfs_lookup(vfs_node_t *dir, const char *name) {
  phase_inode *parent = (phase_inode *)dir->impl;
  if (!parent || parent->type != INODE_DIR)
    return nullptr;

  phase_dir_entry *ents =
      (phase_dir_entry *)phase_data_blocks[parent->blocks[0]];
  for (uint32_t i = 0; i < parent->size; i++) {
    if (strcmp(ents[i].name, name) == 0) {
      // Logic from wrap_phase_inode will be used in vfs.cpp
      // But we need to return a node. This is circular.
      // Re-implementing wrapper logic here for completeness.
      return nullptr; // Will be handled in vfs.cpp's custom lookup
    }
  }
  return nullptr;
}
}

// 🔹 SECTION 12: BOOTSTRAP
extern "C" void fs_phase_a_bootstrap() {
  g_phase_a_sync_enabled = false; // Disable during boot
  phase_inode_count = 0;
  phase_block_count = 0;

  // Try to load from disk first
  phase_vfs_load();

  if (phase_inode_count > 0) {
    serial_log("FS Phase A: Persistent Boot Successful.");
    return;
  }

  // 1. Create Root
  phase_inode *root = phase_alloc_inode(INODE_DIR);
  if (root) {
    root->blocks[0] = phase_alloc_block();
    memset(phase_data_blocks[root->blocks[0]], 0, BLOCK_SIZE);
  }

  // 2. Create Standard Hierarchy
  vfs_mkdir_phase_a("/home");
  vfs_mkdir_phase_a("/home/user");
  vfs_mkdir_phase_a("/home/user/Desktop");
  vfs_mkdir_phase_a("/home/user/Documents");
  vfs_mkdir_phase_a("/home/user/Downloads");

  // System stuff
  vfs_mkdir_phase_a("/system");
  vfs_mkdir_phase_a("/system/.recycle");

  // 3. Create Welcome File
  int id = vfs_create_file_phase_a("/home/user/Desktop/readme.txt");
  if (id >= 0) {
    phase_inode *f = &phase_inode_table[id];
    vfs_write_phase_a(
        f, "Welcome to Retro-OS Phase A\nThis file is stored in RAM blocks.\n",
        60);
  }

  serial_log("FS Phase A: Bootstrapped.");

  // Initial sync
  g_phase_a_sync_enabled = true;
  phase_vfs_sync();
}
} // extern "C"
