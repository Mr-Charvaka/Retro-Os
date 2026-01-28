#include "fat16.h"
#include "../include/dirent.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "../kernel/heap.h"
#include "../kernel/memory.h"
#include "ata.h"
#include "serial.h"

// Forward declaration for DevFS
extern "C" vfs_node_t *devfs_init();
static vfs_node_t *devfs_node = 0;

extern "C" {

static fat16_bpb_t bpb;
static uint32_t root_dir_start_sector;
static uint32_t data_start_sector;
static uint32_t root_sectors;

// Forward Declarations
static uint32_t fat16_write_vfs(vfs_node_t *node, uint32_t offset,
                                uint32_t size, uint8_t *buffer);
static uint32_t fat16_read_vfs(vfs_node_t *node, uint32_t offset, uint32_t size,
                               uint8_t *buffer);

// ============================================================================
// Low Level Helpers - Chote mote kaam
// ============================================================================

static uint32_t fat16_cluster_to_sector(uint16_t cluster) {
  return data_start_sector + (cluster - 2) * bpb.sectors_per_cluster;
}

static uint32_t fat16_get_fat_sector() { return bpb.reserved_sectors; }

static uint16_t fat16_get_fat_entry(uint16_t cluster) {
  uint32_t fat_offset = cluster * 2;
  uint32_t fat_sector = fat16_get_fat_sector() + (fat_offset / 512);
  uint32_t entry_offset = fat_offset % 512;

  uint8_t buffer[512];
  ata_read_sector(fat_sector, buffer);
  return *(uint16_t *)(buffer + entry_offset);
}

static void fat16_set_fat_entry(uint16_t cluster, uint16_t value) {
  uint32_t fat_offset = cluster * 2;
  uint32_t fat_sector = fat16_get_fat_sector() + (fat_offset / 512);
  uint32_t entry_offset = fat_offset % 512;

  uint8_t buffer[512];
  ata_read_sector(fat_sector, buffer);
  *(uint16_t *)(buffer + entry_offset) = value;
  ata_write_sector(fat_sector, buffer);

  // Agar backup FAT hai toh wahan bhi likho
  if (bpb.fats_count > 1) {
    ata_write_sector(fat_sector + bpb.sectors_per_fat, buffer);
  }
}

static uint16_t fat16_alloc_cluster() {
  for (uint16_t cluster = 2; cluster < 0xFFF0; cluster++) {
    if (fat16_get_fat_entry(cluster) == 0x0000) {
      fat16_set_fat_entry(cluster, 0xFFFF); // Mark as EOF (Kaam ho gaya)
      return cluster;
    }
  }
  return 0; // Disk full
}

// 8.3 filename ko insaan ke padhne layak banao
static void fat16_to_name(char *dest, char *src, char *ext) {
  int k = 0;
  for (int j = 0; j < 8; j++)
    if (src[j] != ' ')
      dest[k++] = src[j];

  if (ext[0] != ' ') {
    dest[k++] = '.';
    for (int j = 0; j < 3; j++)
      if (ext[j] != ' ')
        dest[k++] = ext[j];
  }
  dest[k] = 0;
}

// ============================================================================
// Directory Operations
// ============================================================================

// Iterator callback: 0 return karo aage badhne ke liye, 1 agar mil gaya, -1
// error
typedef int (*dir_iterator_t)(fat16_entry_t *entry, uint32_t sector,
                              uint32_t offset, void *data);

// Generic directory iterator (Root (0) aur Subdirs (cluster) dono ke liye
// chalta hai)
static int fat16_iterate_dir(uint16_t dir_cluster, dir_iterator_t callback,
                             void *data) {
  uint8_t buffer[512];

  if (dir_cluster == 0) {
    // Root Directory Iteration
    // serial_log_hex("FAT16: Iterating ROOT dir at sector ",
    //                root_dir_start_sector); // Removed for optimization
    for (uint32_t s = 0; s < root_sectors; s++) {
      ata_read_sector(root_dir_start_sector + s, buffer);
      fat16_entry_t *entries = (fat16_entry_t *)buffer;
      for (int i = 0; i < 16; i++) {
        if (entries[i].filename[0] != 0 &&
            (uint8_t)entries[i].filename[0] != 0xE5) {
          // serial_log("FAT16: Valid Entry found in sector...");
        }
        int res = callback(&entries[i], root_dir_start_sector + s,
                           i * sizeof(fat16_entry_t), data);
        if (res != 0)
          return res;
      }
    }
  } else {
    // Subdirectory Iteration (Cluster Chain)
    uint16_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < 0xFFF0) {
      uint32_t start_sector = fat16_cluster_to_sector(cluster);
      for (int s = 0; s < bpb.sectors_per_cluster; s++) {
        ata_read_sector(start_sector + s, buffer);
        fat16_entry_t *entries = (fat16_entry_t *)buffer;
        for (int i = 0; i < 16; i++) {
          int res = callback(&entries[i], start_sector + s,
                             i * sizeof(fat16_entry_t), data);
          if (res != 0)
            return res;
        }
      }
      cluster = fat16_get_fat_entry(cluster);
    }
  }
  return 0;
}

struct find_ctx {
  const char *name;
  fat16_entry_t result;
  bool found;
  uint32_t sector; // Location of found entry
  uint32_t offset;
};

static int find_callback(fat16_entry_t *entry, uint32_t sector, uint32_t offset,
                         void *d_ptr) {
  find_ctx *ctx = (find_ctx *)d_ptr;

  if (entry->filename[0] == 0)
    return 1; // Directory khatam
  if ((uint8_t)entry->filename[0] == 0xE5)
    return 0; // Deleted hai ye
  if (entry->attributes == ATTR_LFN)
    return 0; // Skip LFN

  char name[13];
  fat16_to_name(name, entry->filename, entry->ext);

  /*serial_log("FAT16 Check: '");
  serial_log(name);
  serial_log("' vs '");
  serial_log(ctx->name);
  serial_log("'\n");*/

  // Case-insensitive comparison
  bool match = true;
  for (int i = 0;; i++) {
    char c1 = name[i];
    char c2 = ctx->name[i];
    if (c1 >= 'a' && c1 <= 'z')
      c1 -= 32;
    if (c2 >= 'a' && c2 <= 'z')
      c2 -= 32;
    if (c1 != c2) {
      match = false;
      break;
    }
    if (c1 == 0)
      break;
  }

  if (match) {
    ctx->result = *entry;
    ctx->found = true;
    ctx->sector = sector;
    ctx->offset = offset;
    return 1; // Stop
  }
  return 0;
}

static bool fat16_find_entry(uint16_t dir_cluster, const char *name,
                             fat16_entry_t *out) {
  find_ctx ctx;
  ctx.name = name;
  ctx.found = false;
  fat16_iterate_dir(dir_cluster, find_callback, &ctx);
  if (ctx.found && out)
    *out = ctx.result;
  return ctx.found;
}

struct create_ctx {
  uint32_t free_sector;
  uint32_t free_offset;
  bool slot_found;
};

static int find_free_callback(fat16_entry_t *entry, uint32_t sector,
                              uint32_t offset, void *d_ptr) {
  create_ctx *ctx = (create_ctx *)d_ptr;
  if (entry->filename[0] == 0 || (uint8_t)entry->filename[0] == 0xE5) {
    ctx->free_sector = sector;
    ctx->free_offset = offset;
    ctx->slot_found = true;
    return 1; // Found slot
  }
  return 0;
}

static int fat16_add_entry(uint16_t dir_cluster, const char *name, uint8_t attr,
                           uint16_t cluster) {
  // Check existence
  if (fat16_find_entry(dir_cluster, name, 0))
    return -1; // Exists

  create_ctx ctx;
  ctx.slot_found = false;
  fat16_iterate_dir(dir_cluster, find_free_callback, &ctx);

  if (!ctx.slot_found) {
    // TODO: Directory ko badhana padega agar root nahi hai
    return -2; // Jagah nahi hai
  }

  // Parse Name
  fat16_entry_t entry;
  memset(&entry, 0, sizeof(fat16_entry_t));
  memset(entry.filename, ' ', 8);
  memset(entry.ext, ' ', 3);

  int fi = 0, in_ext = 0, ei = 0;
  for (int i = 0; name[i]; i++) {
    char ch = name[i];
    if (ch >= 'a' && ch <= 'z')
      ch -= 32;

    if (ch == '.') {
      in_ext = 1;
      ei = 0;
      continue;
    }
    if (in_ext) {
      if (ei < 3)
        entry.ext[ei++] = ch;
    } else {
      if (fi < 8)
        entry.filename[fi++] = ch;
    }
  }

  entry.attributes = attr;
  entry.first_cluster_low = cluster;
  entry.file_size = 0; // 0 for now

  // Write Entry
  uint8_t buffer[512];
  ata_read_sector(ctx.free_sector, buffer);
  memcpy(buffer + ctx.free_offset, &entry, sizeof(fat16_entry_t));
  ata_write_sector(ctx.free_sector, buffer);

  return 0;
}

void fat16_get_stats_bytes(uint32_t *total_bytes, uint32_t *free_bytes) {
  uint32_t total_sectors =
      bpb.total_sectors_16 != 0 ? bpb.total_sectors_16 : bpb.total_sectors_32;
  if (total_bytes)
    *total_bytes = total_sectors * 512;
  if (free_bytes) {
    // Return a reasonable placeholder or count clusters
    // For 32MB image, let's say 25MB is free
    *free_bytes = 25 * 1024 * 1024;
  }
}

// ============================================================================
// Public API Impl
// ============================================================================

void fat16_init() {
  uint8_t sector[512];
  ata_read_sector(0, sector);
  memcpy(&bpb, sector, sizeof(fat16_bpb_t));

  root_dir_start_sector =
      bpb.reserved_sectors + (bpb.fats_count * bpb.sectors_per_fat);
  root_sectors = (bpb.root_entries_count * 32 + 511) / 512;
  data_start_sector = root_dir_start_sector + root_sectors;

  serial_log("FAT16: Subdir support ke saath initialize ho gaya.");
}

fat16_entry_t fat16_find_file(const char *filename) {
  fat16_entry_t out;
  if (fat16_find_entry(0, filename, &out))
    return out;
  memset(&out, 0, sizeof(out));
  return out;
}

void fat16_read_file(fat16_entry_t *entry, uint8_t *buffer) {
  uint16_t cluster = entry->first_cluster_low;
  uint32_t size = entry->file_size;
  uint32_t bytes_read = 0;

  while (cluster >= 2 && cluster < 0xFFF0 && bytes_read < size) {
    uint32_t sector = fat16_cluster_to_sector(cluster);
    for (int i = 0; i < bpb.sectors_per_cluster; i++) {
      ata_read_sector(sector + i, buffer + bytes_read);
      bytes_read += 512;
      if (bytes_read >= size)
        break;
    }
    cluster = fat16_get_fat_entry(cluster);
  }
}

int fat16_write_file(const char *filename, uint8_t *data, uint32_t size) {
  // Legacy wrapper: Seedha root mein likho
  fat16_entry_t entry;
  if (!fat16_find_entry(0, filename, &entry))
    return -1;

  // Create a temporary VFS node to reuse write_vfs logic
  vfs_node_t temp_node;
  memset(&temp_node, 0, sizeof(vfs_node_t));
  strcpy(temp_node.name, filename);
  temp_node.impl = (void *)(uintptr_t)entry.first_cluster_low;
  temp_node.size = entry.file_size;

  // Reuse the logic in write_vfs
  uint32_t written = fat16_write_vfs(&temp_node, 0, size, data);

  // Update directory entry size if changed
  if (written > 0) {
    // Key parameters phir se dhundo directory entry update karne ke liye
    // ... Legacy root write ke liye ye aasaan hai:
    find_ctx ctx;
    ctx.name = filename;
    ctx.found = false;
    fat16_iterate_dir(0, find_callback, &ctx);
    if (ctx.found) {
      uint8_t buffer[512];
      ata_read_sector(ctx.sector, buffer);
      fat16_entry_t *e = (fat16_entry_t *)(buffer + ctx.offset);
      e->file_size = size; // Update size
      e->first_cluster_low =
          (uint16_t)(uintptr_t)
              temp_node.impl; // Update start cluster (if allocated)
      ata_write_sector(ctx.sector, buffer);
    }
  }
  return written;
}

int fat16_create_file(const char *filename) {
  return fat16_add_entry(0, filename, ATTR_ARCHIVE, 0);
}

int fat16_mkdir(const char *name) {
  uint16_t cluster = fat16_alloc_cluster();
  if (cluster == 0)
    return -1;

  // Clear cluster
  uint8_t buffer[512];
  memset(buffer, 0, 512);
  uint32_t sector = fat16_cluster_to_sector(cluster);
  for (int i = 0; i < bpb.sectors_per_cluster; i++)
    ata_write_sector(sector + i, buffer);

  // Add entry to ROOT
  return fat16_add_entry(0, name, ATTR_DIRECTORY, cluster);
}

int fat16_delete_file(const char *name) {
  find_ctx ctx;
  ctx.name = name;
  ctx.found = false;

  // Only supports root delete for legacy API
  fat16_iterate_dir(0, find_callback, &ctx);

  if (ctx.found) {
    // Free Chain
    uint16_t cluster = ctx.result.first_cluster_low;
    while (cluster >= 2 && cluster < 0xFFF0) {
      uint16_t next = fat16_get_fat_entry(cluster);
      fat16_set_fat_entry(cluster, 0);
      cluster = next;
    }

    // Mark Deleted
    uint8_t buffer[512];
    ata_read_sector(ctx.sector, buffer);
    buffer[ctx.offset] = 0xE5;
    ata_write_sector(ctx.sector, buffer);
    return 0;
  }
  return -1;
}

// ============================================================================
// VFS Bindings
// ============================================================================

static struct dirent *fat16_readdir_vfs(vfs_node_t *node, uint32_t index);
static vfs_node_t *fat16_finddir_vfs(vfs_node_t *node, const char *name);
static int fat16_mkdir_vfs(vfs_node_t *node, const char *name, uint32_t mask);
static int fat16_unlink_vfs(vfs_node_t *node, const char *name);
static int fat16_rename_vfs(vfs_node_t *node, const char *old_name,
                            const char *new_name);
static int fat16_create_vfs(vfs_node_t *node, const char *name, int permission);

static uint32_t fat16_write_vfs(vfs_node_t *node, uint32_t offset,
                                uint32_t size, uint8_t *buffer) {
  // Update file size logic simplified
  fat16_entry_t entry;
  if (!fat16_find_entry(0, node->name, &entry)) {
    // node->impl should suffice for writing content
  }

  uint16_t cluster = (uint16_t)(uintptr_t)node->impl;
  if (cluster == 0) {
    // Need to allocate first cluster!
    cluster = fat16_alloc_cluster();
    if (cluster == 0)
      return 0;
    node->impl = (void *)(uintptr_t)cluster;

    // TODO: Directory entry update logic is missing parent context in VFS phase
    // 1
  }

  uint32_t bytes_written = 0;
  // Naive loop
  while (cluster >= 2 && bytes_written < size) {
    uint32_t sector = fat16_cluster_to_sector(cluster);
    for (int i = 0; i < bpb.sectors_per_cluster && bytes_written < size; i++) {
      uint8_t sec_buf[512];
      uint32_t chunk =
          (size - bytes_written) > 512 ? 512 : (size - bytes_written);

      if (chunk < 512)
        ata_read_sector(sector + i, sec_buf);

      memcpy(sec_buf, buffer + bytes_written, chunk);
      ata_write_sector(sector + i, sec_buf);
      bytes_written += chunk;
      if (bytes_written % (64 * 1024) == 0) {
        serial_log("FAT16: Write Progress...");
      }
    }

    if (bytes_written < size) {
      uint16_t next = fat16_get_fat_entry(cluster);
      if (next >= 0xFFF0 || next == 0) {
        uint16_t new_c = fat16_alloc_cluster();
        if (!new_c)
          break;
        fat16_set_fat_entry(cluster, new_c);
        cluster = new_c;
      } else {
        cluster = next;
      }
    }
  }
  return bytes_written;
}

static uint32_t fat16_read_vfs(vfs_node_t *node, uint32_t offset, uint32_t size,
                               uint8_t *buffer) {
  fat16_entry_t entry;
  entry.first_cluster_low = (uint16_t)(uintptr_t)node->impl;
  entry.file_size = node->size;

  // Full read temp buffer strategy
  uint8_t *tmp = (uint8_t *)kmalloc((entry.file_size + 511) & ~511);
  if (!tmp)
    return 0;

  fat16_read_file(&entry, tmp);

  if (offset > entry.file_size) {
    kfree(tmp);
    return 0;
  }
  if (offset + size > entry.file_size)
    size = entry.file_size - offset;

  memcpy(buffer, tmp + offset, size);
  kfree(tmp);
  return size;
}

static vfs_node_t *fat16_finddir_vfs(vfs_node_t *node, const char *name) {
  if (strcmp(name, "dev") == 0 && node->impl == 0) {
    if (!devfs_node)
      devfs_node = devfs_init();
    return devfs_node;
  }

  fat16_entry_t entry;
  if (fat16_find_entry((uint16_t)(uintptr_t)node->impl, name, &entry)) {
    vfs_node_t *res = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    memset(res, 0, sizeof(vfs_node_t));
    strcpy(res->name, name);
    res->size = entry.file_size;
    res->impl = (void *)(uintptr_t)entry.first_cluster_low;
    res->read = fat16_read_vfs;
    res->write = fat16_write_vfs;
    res->readdir = fat16_readdir_vfs;
    res->finddir = fat16_finddir_vfs;
    res->mkdir = fat16_mkdir_vfs;
    res->unlink = fat16_unlink_vfs;
    res->rename = fat16_rename_vfs;
    res->create = fat16_create_vfs;

    if (entry.attributes & ATTR_DIRECTORY) {
      res->flags = VFS_DIRECTORY;
    } else {
      res->flags = VFS_FILE;
    }
    return res;
  }
  return 0;
}

// Readdir Context State
struct readdir_ctx {
  uint32_t target_index;
  uint32_t current_index;
  fat16_entry_t *found_entry;
};

static int readdir_callback(fat16_entry_t *entry, uint32_t sector,
                            uint32_t offset, void *d_ptr) {
  readdir_ctx *ctx = (readdir_ctx *)d_ptr;

  if (entry->filename[0] == 0)
    return 1;
  if ((uint8_t)entry->filename[0] == 0xE5)
    return 0;
  if (entry->attributes & ATTR_VOLUME_ID)
    return 0;

  if (ctx->current_index == ctx->target_index) {
    ctx->found_entry = entry;
    return 1;
  }
  ctx->current_index++;
  return 0;
}

static struct dirent *fat16_readdir_vfs(vfs_node_t *node, uint32_t index) {
  static struct dirent d;
  static fat16_entry_t entry_copy;

  readdir_ctx ctx;
  ctx.target_index = index;
  ctx.current_index = 0;
  ctx.found_entry = 0;

  fat16_iterate_dir((uint16_t)(uintptr_t)node->impl, readdir_callback, &ctx);

  if (ctx.found_entry) {
    entry_copy = *ctx.found_entry;
    memset(&d, 0, sizeof(struct dirent));
    fat16_to_name(d.d_name, entry_copy.filename, entry_copy.ext);
    d.d_ino = entry_copy.first_cluster_low;

    if (entry_copy.attributes & ATTR_DIRECTORY)
      d.d_type = DT_DIR;
    else
      d.d_type = DT_REG;

    return &d;
  }
  return 0;
}

static int fat16_mkdir_vfs(vfs_node_t *node, const char *name, uint32_t mask) {
  uint16_t cluster = fat16_alloc_cluster();
  if (cluster == 0)
    return -1;

  uint8_t buffer[512];
  memset(buffer, 0, 512);
  uint32_t sector = fat16_cluster_to_sector(cluster);
  for (int i = 0; i < bpb.sectors_per_cluster; i++)
    ata_write_sector(sector + i, buffer);

  return fat16_add_entry((uint16_t)(uintptr_t)node->impl, name, ATTR_DIRECTORY,
                         cluster);
}

static int fat16_unlink_vfs(vfs_node_t *node, const char *name) {
  if (node->impl == 0)
    return fat16_delete_file(name);
  return -1;
}

static int fat16_rename_vfs(vfs_node_t *node, const char *old_name,
                            const char *new_name) {
  find_ctx ctx;
  ctx.name = old_name;
  ctx.found = false;
  fat16_iterate_dir((uint16_t)(uintptr_t)node->impl, find_callback, &ctx);

  if (!ctx.found)
    return -1;

  // New name parse
  char filename[9];
  char ext[4];
  memset(filename, ' ', 8);
  memset(ext, ' ', 3);
  filename[8] = 0;
  ext[3] = 0;

  int fi = 0, in_ext = 0, ei = 0;
  for (int i = 0; new_name[i]; i++) {
    char ch = new_name[i];
    if (ch >= 'a' && ch <= 'z')
      ch -= 32; // Uppercase for FAT

    if (ch == '.') {
      in_ext = 1;
      ei = 0;
      continue;
    }
    if (in_ext) {
      if (ei < 3)
        ext[ei++] = ch;
    } else {
      if (fi < 8)
        filename[fi++] = ch;
    }
  }

  serial_log("FAT16: Updating entry to name=");
  serial_log(filename);
  serial_log(" ext=");
  serial_log(ext);

  uint8_t buffer[512];
  ata_read_sector(ctx.sector, buffer);
  fat16_entry_t *entry = (fat16_entry_t *)(buffer + ctx.offset);
  memcpy(entry->filename, filename, 8);
  memcpy(entry->ext, ext, 3);
  ata_write_sector(ctx.sector, buffer);

  return 0;
}

static int fat16_create_vfs(vfs_node_t *node, const char *name,
                            int permission) {
  return fat16_add_entry((uint16_t)(uintptr_t)node->impl, name, ATTR_ARCHIVE,
                         0);
}

vfs_node_t *fat16_vfs_init() {
  vfs_node_t *root = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(root, 0, sizeof(vfs_node_t));
  strcpy(root->name, "/");
  root->flags = VFS_DIRECTORY;
  root->impl = 0; // ROOT CLUSTER

  root->readdir = fat16_readdir_vfs;
  root->finddir = fat16_finddir_vfs;
  root->read = fat16_read_vfs;
  root->write = fat16_write_vfs;
  root->mkdir = fat16_mkdir_vfs;
  root->unlink = fat16_unlink_vfs;
  root->rename = fat16_rename_vfs;
  root->create = fat16_create_vfs;

  // Initialize and mount DevFS
  devfs_node = devfs_init();
  devfs_node->impl = root;

  return root;
}
}
