#include "../include/vfs.h"
#include "../drivers/serial.h"
#include "../drivers/fat32.h"
#include "../include/kernel_fs.h"
#include "../include/kernel_fs_phase3.h"
#include "../include/kernel_vfs_phase4.h"
#include "../include/netfs.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"
#include "tty.h"

extern "C" {

// ============================================================================
// VFS GLOBALS
// ============================================================================

vfs_node_t *vfs_root = 0;
vfs_node_t *vfs_dev = 0;

#define MAX_MOUNTS 16
struct mount_point {
  char path[256];
  struct filesystem *fs;
  vfs_node_t *root;
};

static struct mount_point mounts[MAX_MOUNTS];
static int mount_count = 0;

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static vfs_node_t *alloc_node(const char *name, int type) {
  vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(node, 0, sizeof(vfs_node_t));
  strncpy(node->name, name, 255);
  node->type = (enum vfs_node_type)type; // Cast for safety
  node->ref_count = 1;

  // Default legacy flags mapping for compatibility
  if (type == VFS_DIRECTORY)
    node->flags = 0x02; // VFS_DIRECTORY
  else if (type == VFS_FILE)
    node->flags = 0x01; // VFS_FILE
  else if (type == VFS_DEVICE)
    node->flags = 0x03; // VFS_CHARDEVICE

  return node;
}

// ============================================================================
// RAMFS IMPLEMENTATION (Phase 1 Requirement: Real Folders)
// ============================================================================

int ramfs_create(vfs_node_t *parent, const char *name, int type) {
  vfs_node_t *node = alloc_node(name, type);
  node->type = (type == VFS_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE; // Fix type
  node->parent = parent;
  node->fs = parent->fs;

  // Add to parent's children list
  if (!parent->children) {
    parent->children = node;
  } else {
    vfs_node_t *current = parent->children;
    while (current->next_sibling) {
      current = current->next_sibling;
    }
    current->next_sibling = node;
  }
  return 0;
}

static char tolower(char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int stricmp(const char *s1, const char *s2) {
    while (*s1 && (tolower(*s1) == tolower(*s2))) {
        s1++;
        s2++;
    }
    return (int)(tolower(*s1) - tolower(*s2));
}

vfs_node_t *ramfs_lookup(vfs_node_t *dir, const char *name) {
  vfs_node_t *current = dir->children;
  while (current) {
    if (stricmp(current->name, name) == 0) {
      return current;
    }
    current = current->next_sibling;
  }
  return 0;
}

int ramfs_read(vfs_node_t *file, uint64_t offset, void *buffer, uint64_t size) {
  // RAMFS file reading/writing not fully implemented for data content
  // But structure exists.
  return 0;
}

int ramfs_write(vfs_node_t *file, uint64_t offset, const void *buffer,
                uint64_t size) {
  return 0;
}

// Dirent logic required for sys_readdir
struct dirent ramfs_dirent;
struct dirent *ramfs_readdir(vfs_node_t *dir, uint32_t index) {
  vfs_node_t *current = dir->children;
  uint32_t i = 0;
  while (current) {
    if (i == index) {
      strncpy(ramfs_dirent.d_name, current->name, 255);
      ramfs_dirent.d_ino = current->inode;
      ramfs_dirent.d_type = (current->type == VFS_DIRECTORY) ? 0x02 : 0x01;
      return &ramfs_dirent;
    }
    current = current->next_sibling;
    i++;
  }
  return 0;
}

struct filesystem fs_ram = {.name = "ramfs",
                            .lookup = ramfs_lookup,
                            .create = ramfs_create,
                            .read = ramfs_read,
                            .write = ramfs_write,
                             .readdir = ramfs_readdir};
 
static uint32_t vfs_tty_write(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    return tty_write(tty_get_console(), (const char*)buffer, size);
}
static uint32_t vfs_tty_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    return tty_read(tty_get_console(), (char*)buffer, size);
}

extern "C" int phase_create_in_dir(void *pdir, const char *name, int type);

// Forward declaration of Phase A hooks
static vfs_node_t *vfs_phase_a_lookup(vfs_node_t *dir, const char *name);
static int vfs_phase_a_create_bridge(vfs_node_t *parent, const char *name,
                                     int type);
static int vfs_phase_a_mkdir_bridge(vfs_node_t *parent, const char *name,
                                    uint32_t mode);

struct filesystem fs_phase_a = {
    .name = "phase_a_authority",
    .lookup = vfs_phase_a_lookup,
    .create = vfs_phase_a_create_bridge,
    .mkdir = vfs_phase_a_mkdir_bridge,
};

// ============================================================================
// MOUNT AUTHORITY
// ============================================================================

void vfs_mount(const char *path, struct filesystem *fs, void *device) {
  if (mount_count >= MAX_MOUNTS)
    return;

  vfs_node_t *root = (vfs_node_t *)device;
  if (!root) {
      root = alloc_node("root", VFS_DIRECTORY);
      root->fs = fs;
  }

  // Link mount point into VFS tree if possible (only simple paths like /C handled here)
  if (path[0] == '/' && strlen(path) > 1 && path[1] != '/' && vfs_root) {
      strncpy(root->name, path + 1, 255);
      root->parent = vfs_root;
      root->next_sibling = vfs_root->children;
      vfs_root->children = root;
  }

  strncpy(mounts[mount_count].path, path, 255);
  mounts[mount_count].fs = fs;
  mounts[mount_count].root = root;
  mount_count++;

  serial_log("VFS: Mounted filesystem at:");
  serial_log(path);
}

// ============================================================================
// PATH RESOLUTION (The Core Logic)
// ============================================================================

// Internal: Split path into tokens
static void get_next_token(const char *path, int *offset, char *token) {
  int i = 0;
  while (path[*offset] == '/')
    (*offset)++; // Skip slashes

  while (path[*offset] && path[*offset] != '/') {
    token[i++] = path[(*offset)++];
  }
  token[i] = 0;
}

// ============================================================================
// PHASE A BRIDGE (Phase D Takeover)
// ============================================================================
#include "../include/fs_phase.h"

static vfs_node_t *wrap_phase_inode(phase_inode *pinode, const char *name) {
  if (!pinode)
    return 0;
  vfs_node_t *node =
      alloc_node(name, (pinode->type == INODE_DIR) ? VFS_DIRECTORY : VFS_FILE);
  node->inode = pinode->id;
  node->size = pinode->size;
  node->uid = pinode->owner;
  node->gid = pinode->group;
  node->flags = (pinode->type == INODE_DIR) ? 0x02 : 0x01;
  node->impl = (void *)pinode; // Store the pinode pointer
  node->fs = &fs_phase_a;      // Link to Phase A authority

  // Setup functional bridges
  node->read = [](vfs_node_t *n, uint32_t off, uint32_t sz,
                  uint8_t *buf) -> uint32_t {
    phase_inode *p = (phase_inode *)n->impl;
    if (!p || p->type != INODE_FILE)
      return 0;
    // Simple block 0 read for now (matching Phase A implementation)
    memcpy(buf, phase_data_blocks[p->blocks[0]] + off, sz);
    return sz;
  };

  node->write = [](vfs_node_t *n, uint32_t off, uint32_t sz,
                   uint8_t *buf) -> uint32_t {
    phase_inode *p = (phase_inode *)n->impl;
    if (!p || p->type != INODE_FILE)
      return 0;
    return vfs_write_phase_a(p, (const char *)buf, sz);
  };

  node->readdir = [](vfs_node_t *n, uint32_t idx) -> struct dirent * {
    phase_inode *p = (phase_inode *)n->impl;
    if (!p || p->type != INODE_DIR)
      return (struct dirent *)0;
    phase_dir_entry *ents = (phase_dir_entry *)phase_data_blocks[p->blocks[0]];
    if (idx >= p->size)
      return (struct dirent *)0;

    static struct dirent de;
    strncpy(de.d_name, ents[idx].name, 255);
    de.d_ino = ents[idx].inode_id;
    de.d_type =
        (phase_inode_table[ents[idx].inode_id].type == INODE_DIR) ? 0x02 : 0x01;
    return &de;
  };

  return node;
}

// 🔹 VFS Hooks for Phase A
static vfs_node_t *vfs_phase_a_lookup(vfs_node_t *dir, const char *name) {
  phase_inode *pdir = (phase_inode *)dir->impl;
  if (!pdir || pdir->type != INODE_DIR)
    return nullptr;

  phase_dir_entry *ents = (phase_dir_entry *)phase_data_blocks[pdir->blocks[0]];
  for (uint32_t i = 0; i < pdir->size; i++) {
    if (strcmp(ents[i].name, name) == 0) {
      return wrap_phase_inode(&phase_inode_table[ents[i].inode_id],
                              ents[i].name);
    }
  }
  return nullptr;
}

static int vfs_phase_a_create_bridge(vfs_node_t *parent, const char *name,
                                     int type) {
  if (!parent || !parent->impl)
    return -1;
  return phase_create_in_dir(parent->impl, name, type);
}

static int vfs_phase_a_mkdir_bridge(vfs_node_t *parent, const char *name,
                                    uint32_t mode) {
  return vfs_phase_a_create_bridge(parent, name, 0x02); // VFS_DIRECTORY = 2
}

vfs_node_t *vfs_resolve_path_relative(vfs_node_t *base, const char *path) {
  if (!path)
    return 0;

  // Try Phase A (Authority Layer) first for absolute paths
  if (path[0] == '/') {
    // Optimization: Don't resolve root node as wrapped, use vfs_root (FAT32)
    // for / so lookups can still find things on the physical disk.
    if (strcmp(path, "/") == 0) {
      return vfs_root;
    }

    phase_inode *pinode = phase_vfs_resolve(path);
    if (pinode) {
      // Extract name from path for the node
      const char *last_slash = strrchr(path, '/');
      return wrap_phase_inode(pinode, last_slash ? last_slash + 1 : path);
    }
  }

  vfs_node_t *current = base;
  if (path[0] == '/') {
    current = vfs_root; // Absolute path
  } else if (!current) {
    current = vfs_root; // Fallback
  }

  int offset = 0;
  char token[128];

  while (1) {
    get_next_token(path, &offset, token);
    if (token[0] == 0)
      break; // End of path

    serial_log("VFS: Resolving component: ");
    serial_log(token);

    // --- FLAGSHIP CACHE LOOKUP ---
    vfs_node_t *next = 0;
    
    // Check if child is already in the memory-resident cache
    vfs_node_t *cached = current->children;
    while (cached) {
        if (strcmp(cached->name, token) == 0) {
            next = cached;
            break;
        }
        cached = cached->next_sibling;
    }

    if (!next) {
        // Not in cache, ask the driver
        if (current->finddir) {
            next = current->finddir(current, token);
        } else if (current->fs && current->fs->lookup) {
            next = current->fs->lookup(current, token);
        }

        // If found by driver, add to cache (isolated in memory)
        if (next) {
            next->parent = current;
            next->next_sibling = current->children;
            current->children = next;
        }
    }
    if (!next) {
        serial_log("VFS ERROR: Could not resolve: ");
        serial_log(token);
        return 0; // Not found
    }
    current = next;
  }

  return current;
}

vfs_node_t *vfs_resolve_path(const char *path) {
  return vfs_resolve_path_relative(vfs_root, path);
}

// ============================================================================
// SYSTEM CALLS WRAPPERS (Interfaces)
// ============================================================================

int vfs_read(vfs_node_t *node, uint64_t offset, void *buf, uint64_t size) {
  if (!node)
    return 0;
  if (node->read)
    return node->read(node, (uint32_t)offset, (uint32_t)size,
                      (uint8_t *)buf); // Wrapped pointer
  if (node->fs && node->fs->read)
    return node->fs->read(node, offset, buf, size);
  return 0;
}

int vfs_write(vfs_node_t *node, uint64_t offset, const void *buf,
              uint64_t size) {
  if (!node)
    return 0;
  if (node->write)
    return node->write(node, (uint32_t)offset, (uint32_t)size,
                       (uint8_t *)buf); // Wrapped pointer
  if (node->fs && node->fs->write)
    return node->fs->write(node, offset, buf, size);
  return 0;
}

int vfs_create(const char *path, int type) {
  serial_log("VFS: Creating ");
  serial_log(path);

  // Try Phase A creation for known paths
  if (path[0] == '/') {
    if (type == VFS_DIRECTORY) {
      if (vfs_mkdir_phase_a(path))
        return 0;
    } else {
      if (vfs_create_file_phase_a(path) >= 0)
        return 0;
    }
  }

  // Need to resolve parent
  char parent_path[256];
  char name[128];

  int len = strlen(path);
  int last_slash = -1;
  for (int i = len - 1; i >= 0; i--) {
    if (path[i] == '/') {
      last_slash = i;
      break;
    }
  }

  vfs_node_t *parent = vfs_root;
  if (last_slash > 0) {
    memcpy(parent_path, path, last_slash);
    parent_path[last_slash] = 0;
    parent = vfs_resolve_path(parent_path);
  } else if (last_slash == 0) {
    parent = vfs_root;
  }

  if (last_slash != -1)
    strcpy(name, path + last_slash + 1);
  else
    strcpy(name, path);

  if (!parent)
    return -1;

  // If we're creating a directory, try mkdir first
  if (type == VFS_DIRECTORY) {
    if (parent->mkdir)
      return parent->mkdir(parent, name, 0755);
    if (parent->fs && parent->fs->mkdir)
      return parent->fs->mkdir(parent, name, 0755);
  }

  // Check legacy create first
  if (parent->create) {
    return parent->create(parent, name, type);
  }
  // Then FS interface
  if (parent->fs && parent->fs->create) {
    return parent->fs->create(parent, name, type);
  }
  return -1;
}

struct dirent *vfs_readdir(vfs_node_t *node, uint32_t index) {
  if (!node)
    return 0;
  if (node->readdir)
    return node->readdir(node, index); // Legacy
  if (node->fs && node->fs->readdir)
    return node->fs->readdir(node, index);
  return 0;
}

// ============================================================================
// INIT
// ============================================================================

extern "C" vfs_node_t *fat32_vfs_init(fat32_context_t *ctx);

extern "C" void phase_a_set_sync(bool enabled);

fat32_context_t *system_fat32_ctx = nullptr;

#include "../include/mbr.h"
#include "../drivers/ntfs.h"
void vfs_init() {
  serial_log("VFS: Flagship Initialisation Sequence...");

  // 1. Initialise RAMFS for root
  vfs_root = alloc_node("/", VFS_DIRECTORY);
  vfs_root->fs = &fs_ram;
  vfs_mount("/", &fs_ram, 0);

  // 2. Initialise Phase A persistence system
  fs_phase_a_bootstrap();

  // 3. Mount Physical Partitions
  partition_info_t parts[MAX_PARTITIONS_PER_DISK];
  
  // -- Drive 0 (Primary) --
  int n0 = mbr_enumerate_partitions(0, parts, MAX_PARTITIONS_PER_DISK);
  if (n0 > 0) {
      for (int i = 0; i < n0; i++) {
          if (parts[i].type == PART_TYPE_FAT32 || parts[i].type == PART_TYPE_FAT32_LBA) {
              fat32_context_t *ctx = fat32_mount(0, parts[i].start_lba);
              if (ctx) {
                  vfs_node_t *r = fat32_vfs_init(ctx);
                  if (i == 0) { vfs_mount("/C", r->fs, r); system_fat32_ctx = ctx; }
                  else { char path[4] = "/D"; path[2] = 'D' + (i-1); vfs_mount(path, r->fs, r); }
              }
          }
      }
  } else {
      fat32_context_t *raw = fat32_mount(0, 0);
      if (raw) { vfs_node_t *r = fat32_vfs_init(raw); vfs_mount("/C", r->fs, r); system_fat32_ctx = raw; }
  }

  // -- Drive 1 (Secondary) --
  int n1 = mbr_enumerate_partitions(1, parts, MAX_PARTITIONS_PER_DISK);
  if (n1 > 0) {
      vfs_create("/storage", VFS_DIRECTORY);
      for (int i = 0; i < n1; i++) {
          char mount_path[32];
          char pnum[8];
          strcpy(mount_path, "/storage/p");
          itoa(i + 1, pnum, 10);
          strcat(mount_path, pnum);
          
          vfs_create(mount_path, VFS_DIRECTORY);

          if (parts[i].type == PART_TYPE_FAT32 || parts[i].type == PART_TYPE_FAT32_LBA) {
              fat32_context_t *ctx = fat32_mount(1, parts[i].start_lba);
              if (ctx) {
                  vfs_node_t *r = fat32_vfs_init(ctx);
                  vfs_mount(mount_path, r->fs, r);
                  serial_log("VFS: Mounted FAT32 Partition at ");
                  serial_log(mount_path);
              }
          } else if (parts[i].type == PART_TYPE_NTFS) {
              ntfs_context_t *ctx = ntfs_mount(1, parts[i].start_lba);
              if (ctx) { 
                  vfs_node_t *r = ntfs_vfs_init(ctx); 
                  if (r) { 
                      vfs_mount(mount_path, &fs_ram, r); 
                      serial_log("VFS: Mounted NTFS Partition at ");
                      serial_log(mount_path);
                  }
              }
          }
      }
  }

  serial_log("VFS_INIT: Creating standard paths...");
  phase_a_set_sync(false);

  vfs_create("/home", VFS_DIRECTORY);
  vfs_create("/home/user", VFS_DIRECTORY);
  vfs_create("/home/user/Desktop", VFS_DIRECTORY);
  // Flagship Shortcuts (Physical Files)
  vfs_create("/home/user/Desktop/Explorer.lnk", VFS_FILE);
  vfs_create("/home/user/Desktop/Terminal.lnk", VFS_FILE);
  vfs_create("/home/user/Desktop/Calculator.lnk", VFS_FILE);
  vfs_create("/home/user/Desktop/Browser.lnk", VFS_FILE);
  vfs_create("/home/user/Desktop/NetTest.lnk", VFS_FILE);
  // Removed office, notepad & browser shortcuts
  vfs_create("/home/user/Documents", VFS_DIRECTORY);
  vfs_create("/home/user/Pictures", VFS_DIRECTORY);
  vfs_create("/home/user/Music", VFS_DIRECTORY);
  vfs_create("/home/user/Videos", VFS_DIRECTORY);
  vfs_create("/home/user/Downloads", VFS_DIRECTORY);
  vfs_create("/home/user/Projects", VFS_DIRECTORY);
  vfs_create("/system", VFS_DIRECTORY);
  // System-internal directories (Stay in RAMFS for speed and consistency)
  ramfs_create(vfs_root, "dev", VFS_DIRECTORY);
  ramfs_create(vfs_root, "tmp", VFS_DIRECTORY);
  
  vfs_dev = vfs_resolve_path("/dev");
  if (vfs_dev) {
    ramfs_create(vfs_dev, "tty", VFS_DEVICE);
    vfs_node_t *tty_node = vfs_resolve_path("/dev/tty");
    if (tty_node) {
        tty_node->write = vfs_tty_write;
        tty_node->read = vfs_tty_read;
    }
  }

  vfs_create("/C/Windows", VFS_DIRECTORY);
  vfs_create("/C/Windows/System32", VFS_DIRECTORY);

  // NETFS
  netfs_init();
  vfs_node_t *net_node = netfs_mount("10.0.2.2", "Network");
  if (net_node) {
      vfs_node_t *c_node = vfs_resolve_path("/C");
      if (c_node) {
          net_node->parent = c_node;
          net_node->next_sibling = c_node->children;
          c_node->children = net_node;
      }
  }

  vfs_create("/home/user/Desktop/Welcome.txt", VFS_FILE);
  vfs_node_t *welcome = vfs_resolve_path("/home/user/Desktop/Welcome.txt");
  if (welcome) {
      const char *msg = "Retro-OS Flagship Storage Layer Active.\n/dev/tty correctly linked.";
      vfs_write(welcome, 0, (const uint8_t *)msg, strlen(msg));
  }

  phase_a_set_sync(true);
  phase_vfs_sync();
  vfs_dev = vfs_resolve_path("/dev");

#include "../include/kernel_vfs_phase4.h"
  fs_phase3_init();
  vfs_phase4_init();
}

// Wrapper for legacy internal calls (Kernel.cpp might call these if looking for
// old API) These map legacy read_vfs calls to new vfs_read
uint32_t read_vfs(vfs_node_t *node, uint32_t offset, uint32_t size,
                  uint8_t *buffer) {
  return vfs_read(node, offset, buffer, size);
}
uint32_t write_vfs(vfs_node_t *node, uint32_t offset, uint32_t size,
                   uint8_t *buffer) {
  return vfs_write(node, offset, buffer, size);
}
void open_vfs(vfs_node_t *node, uint8_t read, uint8_t write) {
  if (node && node->open)
    node->open(node);
}
void close_vfs(vfs_node_t *node) {
  if (node && node->close)
    node->close(node);
}
struct dirent *readdir_vfs(vfs_node_t *node, uint32_t index) {
  return vfs_readdir(node, index);
}
vfs_node_t *finddir_vfs(vfs_node_t *node, const char *name) {
  // Helper to just trigger lookup on a node
  if (node->finddir)
    return node->finddir(node, name);
  if (node->fs && node->fs->lookup)
    return node->fs->lookup(node, name);
  return 0;
}
int mkdir_vfs(vfs_node_t *node, const char *name, uint32_t mask) {
  if (node->mkdir)
    return node->mkdir(node, name, mask);
  if (node->fs && node->fs->mkdir)
    return node->fs->mkdir(node, name, mask);
  if (node->fs && node->fs->create)
    return node->fs->create(node, name, VFS_DIRECTORY);
  return -1;
}
// Global helpers
// Global helpers
int vfs_mkdir(const char *path, uint32_t mode) {
  (void)mode; // Ignored for now, or pass to create
  return vfs_create(path, VFS_DIRECTORY);
}

int vfs_unlink(const char *path) {
  vfs_node_t *node = vfs_resolve_path(path);
  if (!node)
    return -1;
  if (!node->parent)
    return -1; // Cannot unlink root

  if (node->parent->unlink)
    return node->parent->unlink(node->parent, node->name);
  if (node->parent->fs && node->parent->fs->unlink)
    return node->parent->fs->unlink(node->parent, node->name);

  return -1;
}

int vfs_rmdir(const char *path) {
  vfs_node_t *node = vfs_resolve_path(path);
  if (!node)
    return -1;
  if (node->type != VFS_DIRECTORY)
    return -1; // Not a directory
  if (!node->parent)
    return -1; // Cannot rmdir root

  if (node->parent->rmdir)
    return node->parent->rmdir(node->parent, node->name);
  if (node->parent->fs && node->parent->fs->rmdir)
    return node->parent->fs->rmdir(node->parent, node->name);

  return -1;
}

void vfs_close(vfs_node_t *node) {
  if (!node)
    return;
  node->ref_count--;
  if (node->ref_count == 0 && node != vfs_root && node != vfs_dev) {
    if (node->close)
      node->close(node);
    if (node->fs && node->fs->close)
      node->fs->close(node);
    kfree(node);
  }
}

int vfs_rename(const char *oldpath, const char *newpath) {
  char old_parent_path[256], old_name[128];
  char new_parent_path[256], new_name[128];

  auto split = [](const char *path, char *pdir, char *pname) {
    int len = strlen(path);
    int last_slash = -1;
    for (int i = len - 1; i >= 0; i--) {
      if (path[i] == '/') {
        last_slash = i;
        break;
      }
    }
    if (last_slash == -1) {
      strcpy(pdir, ".");
      strcpy(pname, path);
    } else if (last_slash == 0) {
      strcpy(pdir, "/");
      strcpy(pname, path + 1);
    } else {
      memcpy(pdir, path, last_slash);
      pdir[last_slash] = 0;
      strcpy(pname, path + last_slash + 1);
    }
  };

  serial_log("VFS: Renaming ");
  serial_log(oldpath);
  serial_log(" to ");
  serial_log(newpath);

  split(oldpath, old_parent_path, old_name);
  split(newpath, new_parent_path, new_name);

  serial_log("VFS: Parent resolved as=");
  serial_log(old_parent_path);

  if (strcmp(old_parent_path, new_parent_path) != 0) {
    serial_log("VFS: Rename across directories not supported yet.");
    return -1;
  }

  vfs_node_t *parent = vfs_resolve_path(old_parent_path);
  if (!parent)
    return -1;

  if (parent->rename)
    return parent->rename(parent, old_name, new_name);
  if (parent->fs && parent->fs->rename)
    return parent->fs->rename(parent, old_name, new_name);

  return -1;
}

int unlink_vfs(vfs_node_t *node, const char *name) {
  if (node->fs && node->fs->unlink)
    return node->fs->unlink(node, name);
  return -1;
}

int rmdir_vfs(vfs_node_t *node, const char *name) {
  if (node->fs && node->fs->rmdir)
    return node->fs->rmdir(node, name);
  return -1;
}

int vfs_get_mount_count() { return mount_count; }
const char *vfs_get_mount_path(int i) {
  if (i >= 0 && i < mount_count)
    return mounts[i].path;
  return 0;
}

int vfs_get_disk_usage(const char *path, uint32_t *total, uint32_t *free) {
  vfs_node_t *node = vfs_resolve_path(path);
  if (!node)
    return -1;
  if (node->device) {
    fat32_get_stats((fat32_context_t *)node->device, total, free);
    return 0;
  }
  *total = 2147483648U; // 2GB
  *free = 1073741824U;  // 1GB
  return 0;
}

} // extern "C"
