// DevFS - Device Filesystem Implementation
// Provides /dev/null, /dev/zero, /dev/tty

#include "../include/string.h"
#include "../include/vfs.h"
#include "../kernel/heap.h"
#include "../kernel/memory.h"
#include "serial.h"

#include "../kernel/tty.h"
#include "serial.h"

extern "C" {
extern tty_t *tty_get_console();
extern int tty_read(tty_t *tty, char *buf, int len);
extern int tty_write(tty_t *tty, const char *buf, int len);

// Static device nodes - never freed
static vfs_node_t *devfs_root = 0;
static vfs_node_t *null_node = 0;
static vfs_node_t *zero_node = 0;
static vfs_node_t *tty_node = 0;
static vfs_node_t *ptmx_node = 0;
static vfs_node_t *pts_dir_node = 0;

static int atoi(const char *str) {
  int res = 0;
  for (int i = 0; str[i] != '\0'; ++i)
    res = res * 10 + str[i] - '0';
  return res;
}

extern "C" {
vfs_node_t *pty_get_slave_node(int index);
int pty_get_max_ptys();
bool pty_is_active(int index);
}

// ============================================================================
// /dev/null - Read returns EOF, Write discards
// ============================================================================
static uint32_t null_read(vfs_node_t *node, uint32_t offset, uint32_t size,
                          uint8_t *buffer) {
  (void)node;
  (void)offset;
  (void)size;
  (void)buffer;
  return 0; // EOF
}

static uint32_t null_write(vfs_node_t *node, uint32_t offset, uint32_t size,
                           uint8_t *buffer) {
  (void)node;
  (void)offset;
  (void)buffer;
  return size; // Accept everything, discard
}

// ============================================================================
// /dev/zero - Read returns zeros, Write discards
// ============================================================================
static uint32_t zero_read(vfs_node_t *node, uint32_t offset, uint32_t size,
                          uint8_t *buffer) {
  (void)node;
  (void)offset;
  memset(buffer, 0, size);
  return size;
}

static uint32_t zero_write(vfs_node_t *node, uint32_t offset, uint32_t size,
                           uint8_t *buffer) {
  (void)node;
  (void)offset;
  (void)buffer;
  return size; // Discard
}

// ============================================================================
// /dev/tty - Terminal placeholder
// ============================================================================
static uint32_t dev_tty_read(vfs_node_t *node, uint32_t offset, uint32_t size,
                             uint8_t *buffer) {
  (void)node;
  (void)offset;
  return tty_read(tty_get_console(), (char *)buffer, size);
}

static uint32_t dev_tty_write(vfs_node_t *node, uint32_t offset, uint32_t size,
                              uint8_t *buffer) {
  (void)node;
  (void)offset;
  return tty_write(tty_get_console(), (const char *)buffer, size);
}

// ============================================================================
// DevFS Directory Operations
// ============================================================================
static struct dirent devfs_dirent;

static struct dirent *devfs_readdir(vfs_node_t *node, uint32_t index) {
  (void)node;
  if (index == 0) {
    strcpy(devfs_dirent.d_name, "null");
    devfs_dirent.d_ino = 1;
    return &devfs_dirent;
  }
  if (index == 1) {
    strcpy(devfs_dirent.d_name, "zero");
    devfs_dirent.d_ino = 2;
    return &devfs_dirent;
  }
  if (index == 2) {
    strcpy(devfs_dirent.d_name, "tty");
    devfs_dirent.d_ino = 3;
    return &devfs_dirent;
  }
  if (index == 3) {
    strcpy(devfs_dirent.d_name, "pts");
    devfs_dirent.d_ino = 4;
    return &devfs_dirent;
  }
  if (index == 4) {
    strcpy(devfs_dirent.d_name, "ptmx");
    devfs_dirent.d_ino = 5;
    return &devfs_dirent;
  }
  return 0;
}

static struct dirent *pts_readdir(vfs_node_t *node, uint32_t index) {
  int count = 0;
  int max = pty_get_max_ptys();
  for (int i = 0; i < max; i++) {
    if (pty_is_active(i)) {
      if (count == (int)index) {
        itoa(i, devfs_dirent.d_name, 10);
        devfs_dirent.d_ino = 100 + i;
        return &devfs_dirent;
      }
      count++;
    }
  }
  return 0;
}

static vfs_node_t *pts_finddir(vfs_node_t *node, const char *name) {
  int idx = atoi(name);
  return pty_get_slave_node(idx);
}

static vfs_node_t *devfs_finddir(vfs_node_t *node, const char *name) {
  (void)node;
  if (strcmp(name, "null") == 0)
    return null_node;
  if (strcmp(name, "zero") == 0)
    return zero_node;
  if (strcmp(name, "tty") == 0)
    return tty_node;
  if (strcmp(name, "pts") == 0)
    return pts_dir_node;
  if (strcmp(name, "ptmx") == 0)
    return ptmx_node;
  return 0;
}

// ============================================================================
// DevFS Initialization
// ============================================================================
vfs_node_t *devfs_init() {
  serial_log("DEVFS: Initializing...");

  // Create root /dev directory
  devfs_root = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(devfs_root, 0, sizeof(vfs_node_t));
  strcpy(devfs_root->name, "dev");
  devfs_root->flags = VFS_DIRECTORY;
  devfs_root->readdir = devfs_readdir;
  devfs_root->finddir = devfs_finddir;
  devfs_root->ref_count = 0xFFFFFFFF; // Never free

  // Create /dev/null
  null_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(null_node, 0, sizeof(vfs_node_t));
  strcpy(null_node->name, "null");
  null_node->flags = VFS_DEVICE;
  null_node->read = null_read;
  null_node->write = null_write;
  null_node->ref_count = 0xFFFFFFFF;

  // Create /dev/zero
  zero_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(zero_node, 0, sizeof(vfs_node_t));
  strcpy(zero_node->name, "zero");
  zero_node->flags = VFS_DEVICE;
  zero_node->read = zero_read;
  zero_node->write = zero_write;
  zero_node->ref_count = 0xFFFFFFFF;

  // Create /dev/tty
  tty_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(tty_node, 0, sizeof(vfs_node_t));
  strcpy(tty_node->name, "tty");
  tty_node->flags = VFS_DEVICE;
  tty_node->read = dev_tty_read;
  tty_node->write = dev_tty_write;
  tty_node->ref_count = 0xFFFFFFFF;

  // Create /dev/pts
  pts_dir_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(pts_dir_node, 0, sizeof(vfs_node_t));
  strcpy(pts_dir_node->name, "pts");
  pts_dir_node->flags = VFS_DIRECTORY;
  pts_dir_node->readdir = pts_readdir;
  pts_dir_node->finddir = pts_finddir;
  pts_dir_node->ref_count = 0xFFFFFFFF;

  // Create /dev/ptmx (Placeholder)
  ptmx_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(ptmx_node, 0, sizeof(vfs_node_t));
  strcpy(ptmx_node->name, "ptmx");
  ptmx_node->flags = VFS_DEVICE;
  ptmx_node->ref_count = 0xFFFFFFFF;

  serial_log("DEVFS: Initialized.");
  return devfs_root;
}

} // extern "C"
