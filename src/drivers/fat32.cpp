#include "fat32.h"
#include "../include/dirent.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "../kernel/heap.h"
#include "../kernel/memory.h"
#include "ata.h"
#include "serial.h"

#include "../kernel/process.h"
 
extern "C" {
extern fat32_context_t *system_fat32_ctx;
extern process_t *current_process;

/* ================================================================
   INTERNAL HELPERS
   ================================================================ */

/**
 * Convert a cluster number to an absolute LBA sector.
 * Cluster 2 = first data sector (data_start_sector).
 */
static uint32_t fat32_cluster_to_lba(fat32_context_t *ctx, uint32_t cluster)
{
    return ctx->data_start_sector + (cluster - 2) * ctx->bpb.sectors_per_cluster;
}

/**
 * Read a single FAT entry for a given cluster.
 * Returns 0x0FFFFFFF (EOC) on read error.
 */
static uint32_t fat32_get_fat_entry(fat32_context_t *ctx, uint32_t cluster)
{
    uint32_t fat_byte_offset = cluster * 4;
    uint32_t fat_sector      = ctx->fat_start_sector + (fat_byte_offset / 512);
    uint32_t entry_offset    = fat_byte_offset % 512;

    uint8_t buffer[512];
    if (!ata_read_sector_ext(ctx->drive_id, fat_sector, buffer)) {
        serial_log("FAT32: ERROR reading FAT sector");
        return 0x0FFFFFFF;
    }

    uint32_t val = *(uint32_t *)(buffer + entry_offset);
    return val & 0x0FFFFFFF;
}

/**
 * Write a FAT entry to both FAT1 and FAT2.
 */
static void fat32_set_fat_entry(fat32_context_t *ctx, uint32_t cluster, uint32_t value)
{
    uint32_t fat_byte_offset = cluster * 4;
    uint32_t sector_offset   = fat_byte_offset / 512;
    uint32_t entry_offset    = fat_byte_offset % 512;

    uint8_t buffer[512];

    /* Write to FAT1 */
    uint32_t fat1_sector = ctx->fat_start_sector + sector_offset;
    if (ata_read_sector_ext(ctx->drive_id, fat1_sector, buffer)) {
        *(uint32_t *)(buffer + entry_offset) = value & 0x0FFFFFFF;
        ata_write_sector_ext(ctx->drive_id, fat1_sector, buffer);
    }

    /* Write to FAT2 (mirror) */
    uint32_t fat2_sector = ctx->fat_start_sector
                         + ctx->bpb.sectors_per_fat_32
                         + sector_offset;
    if (ata_read_sector_ext(ctx->drive_id, fat2_sector, buffer)) {
        *(uint32_t *)(buffer + entry_offset) = value & 0x0FFFFFFF;
        ata_write_sector_ext(ctx->drive_id, fat2_sector, buffer);
    }
}

/**
 * Allocate a new cluster, zero it, and mark it EOC.
 * Returns 0 on failure.
 */
static uint32_t fat32_alloc_cluster(fat32_context_t *ctx)
{
    /* Start search from cluster 3 (2 is root dir) */
    for (uint32_t c = 3; c < 0x0FFFFFF0; c++) {
        if (fat32_get_fat_entry(ctx, c) == 0) {
            fat32_set_fat_entry(ctx, c, 0x0FFFFFFF); /* EOC */

            /* Zero the new cluster */
            uint32_t lba = fat32_cluster_to_lba(ctx, c);
            uint8_t zero[512];
            memset(zero, 0, 512);
            for (int i = 0; i < ctx->bpb.sectors_per_cluster; i++) {
                ata_write_sector_ext(ctx->drive_id, lba + i, zero);
            }
            return c;
        }
    }
    serial_log("FAT32: ERROR disk full, no free clusters");
    return 0;
}

/**
 * Convert a FAT 8.3 directory entry name (space-padded) to a
 * normal null-terminated string like "HELLO.ELF".
 */
static void fat32_83_to_name(char *dest, const char *name8, const char *ext3)
{
    int k = 0;
    for (int i = 0; i < 8 && name8[i] != ' '; i++) {
        dest[k++] = name8[i];
    }
    if (ext3[0] != ' ' && ext3[0] != '\0') {
        dest[k++] = '.';
        for (int i = 0; i < 3 && ext3[i] != ' '; i++) {
            dest[k++] = ext3[i];
        }
    }
    dest[k] = '\0';
}

/**
 * Case-insensitive string compare.
 * Returns true if strings are equal ignoring case.
 */
static bool fat32_name_match(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return false;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/* ================================================================
   DIRECTORY ITERATOR
   ================================================================ */

typedef int (*dir_cb_t)(fat32_context_t *ctx,
                        fat32_entry_t   *entry,
                        uint32_t         sector,
                        uint32_t         offset_in_sector,
                        void            *userdata);

static int fat32_iterate_dir(fat32_context_t *ctx,
                             uint32_t         dir_cluster,
                             dir_cb_t         callback,
                             void            *userdata)
{
    uint32_t cluster = (dir_cluster == 0) ? ctx->bpb.root_cluster : dir_cluster;
    uint8_t buffer[512];

    while (cluster >= 2 && cluster < 0x0FFFFFF0) {
        uint32_t lba = fat32_cluster_to_lba(ctx, cluster);

        for (int s = 0; s < ctx->bpb.sectors_per_cluster; s++) {
            if (!ata_read_sector_ext(ctx->drive_id, lba + s, buffer)) {
                serial_log("FAT32: ERROR reading dir sector");
                return -1;
            }

            fat32_entry_t *entries = (fat32_entry_t *)buffer;
            int entries_per_sector = 512 / sizeof(fat32_entry_t);

            for (int i = 0; i < entries_per_sector; i++) {
                fat32_entry_t *e = &entries[i];

                if ((uint8_t)e->filename[0] == 0x00) return 0;
                if ((uint8_t)e->filename[0] == 0xE5) continue;
                if (e->attributes == ATTR_LFN) continue;
                if (e->attributes & ATTR_VOLUME_ID) continue;

                int res = callback(ctx, e, lba + s, i * sizeof(fat32_entry_t), userdata);
                if (res != 0) return res;
            }
        }
        cluster = fat32_get_fat_entry(ctx, cluster);
    }
    return 0;
}

/* ================================================================
   MOUNT
   ================================================================ */

fat32_context_t *fat32_mount(uint8_t drive_id, uint32_t start_lba)
{
    uint8_t sector0[512];
    serial_log("FAT32: Mounting drive...");

    if (!ata_read_sector_ext(drive_id, start_lba, sector0)) {
        serial_log("FAT32: ERROR could not read BPB sector");
        return nullptr;
    }

    fat32_context_t *ctx = (fat32_context_t *)kmalloc(sizeof(fat32_context_t));
    if (!ctx) return nullptr;
    memset(ctx, 0, sizeof(fat32_context_t));

    ctx->drive_id        = drive_id;
    ctx->partition_start = start_lba;
    memcpy(&ctx->bpb, sector0, sizeof(fat32_bpb_t));

    if (ctx->bpb.bytes_per_sector != 512) {
        serial_log("FAT32: ERROR invalid BPB");
        return nullptr;
    }

    ctx->fat_start_sector  = start_lba + ctx->bpb.reserved_sectors;
    ctx->data_start_sector = ctx->fat_start_sector
                           + (ctx->bpb.fats_count * ctx->bpb.sectors_per_fat_32);

    ctx->total_sectors = (ctx->bpb.total_sectors_16 != 0)
                       ? ctx->bpb.total_sectors_16
                       : ctx->bpb.total_sectors_32;

    serial_log("FAT32: Mount OK");
    return ctx;
}

/* ================================================================
   VFS LAYER ADAPTERS
   ================================================================ */

static uint32_t fat32_read_vfs(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !node->device || !buffer || size == 0) return 0;
    fat32_context_t *ctx = (fat32_context_t *)node->device;
    uint32_t cluster = (uint32_t)(uintptr_t)node->impl;

    if (offset >= (uint32_t)node->size) return 0;
    if (offset + size > (uint32_t)node->size) size = (uint32_t)node->size - offset;

    uint32_t bytes_read = 0;
    uint32_t file_pos = 0;
    uint32_t cluster_bytes = ctx->bpb.sectors_per_cluster * 512;

    while (cluster >= 2 && cluster < 0x0FFFFFF0 && bytes_read < size) {
        uint32_t cluster_start = file_pos;
        uint32_t cluster_end = file_pos + cluster_bytes;

        if (cluster_end > offset && cluster_start < offset + size) {
            uint32_t lba = fat32_cluster_to_lba(ctx, cluster);
            for (int s = 0; s < ctx->bpb.sectors_per_cluster && bytes_read < size; s++) {
                uint32_t sector_start = file_pos;
                uint32_t sector_end = file_pos + 512;

                if (sector_end > offset && sector_start < offset + size) {
                    uint8_t sector_buf[512];
                    if (!ata_read_sector_ext(ctx->drive_id, lba + s, sector_buf)) return bytes_read;

                    uint32_t copy_start = (offset > sector_start) ? (offset - sector_start) : 0;
                    uint32_t copy_end = ((offset + size) < sector_end) ? (offset + size - sector_start) : 512;
                    uint32_t copy_len = copy_end - copy_start;
                    uint32_t dest_pos = (sector_start + copy_start) - offset;

                    memcpy(buffer + dest_pos, sector_buf + copy_start, copy_len);
                    bytes_read += copy_len;
                }
                file_pos += 512;
            }
        } else {
            file_pos += cluster_bytes;
        }
        cluster = fat32_get_fat_entry(ctx, cluster);
    }
    return bytes_read;
}

struct finddir_ctx {
    const char *target_name;
    fat32_entry_t result;
    bool found;
};

static int finddir_callback(fat32_context_t *ctx, fat32_entry_t *entry, uint32_t sector, uint32_t offset, void *userdata)
{
    (void)ctx; (void)sector; (void)offset;
    struct finddir_ctx *fc = (struct finddir_ctx *)userdata;
    char name[13];
    fat32_83_to_name(name, entry->filename, entry->ext);

    if (fat32_name_match(name, fc->target_name)) {
        fc->result = *entry;
        fc->found = true;
        return 1;
    }
    return 0;
}

static vfs_node_t *fat32_finddir_vfs(vfs_node_t *node, const char *name)
{
    if (!node || !node->device || !name) return nullptr;
    fat32_context_t *ctx = (fat32_context_t *)node->device;
    uint32_t dir_cluster = (uint32_t)(uintptr_t)node->impl;

    struct finddir_ctx fc;
    fc.target_name = name;
    fc.found = false;

    fat32_iterate_dir(ctx, dir_cluster, finddir_callback, &fc);

    if (!fc.found) return nullptr;

    uint32_t start_cluster = ((uint32_t)fc.result.first_cluster_high << 16) | fc.result.first_cluster_low;
    vfs_node_t *res = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    memset(res, 0, sizeof(vfs_node_t));

    strncpy(res->name, name, 255);
    res->size = fc.result.file_size;
    res->device = ctx;
    res->impl = (void *)(uintptr_t)start_cluster;
    res->read = fat32_read_vfs;
    res->finddir = fat32_finddir_vfs;
    res->type = (fc.result.attributes & ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
    res->flags = res->type;

    return res;
}

static struct filesystem fs_fat32 = {
    .name = "fat32",
    .lookup = fat32_finddir_vfs,
    .create = [](vfs_node_t *parent, const char *name, int type) -> int {
        fat32_context_t *ctx = (fat32_context_t *)parent->device;
        if (type == VFS_DIRECTORY) return fat32_mkdir(current_process ? ctx : system_fat32_ctx, name);
        return fat32_create_file(current_process ? ctx : system_fat32_ctx, name);
    },
    .mkdir = [](vfs_node_t *parent, const char *name, uint32_t mode) -> int {
        (void)mode;
        fat32_context_t *ctx = (fat32_context_t *)parent->device;
        return fat32_mkdir(ctx, name);
    },
    .unlink = [](vfs_node_t *parent, const char *name) -> int {
        fat32_context_t *ctx = (fat32_context_t *)parent->device;
        return fat32_delete_file(ctx, name);
    }
};

vfs_node_t *fat32_vfs_init(fat32_context_t *ctx)
{
    if (!ctx) return nullptr;
    vfs_node_t *root = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    memset(root, 0, sizeof(vfs_node_t));
    strncpy(root->name, "C", 255);
    root->type = VFS_DIRECTORY;
    root->device = ctx;
    root->fs = &fs_fat32;
    root->impl = (void *)(uintptr_t)ctx->bpb.root_cluster;
    root->finddir = fat32_finddir_vfs;
    return root;
}

/* ================================================================
   PUBLIC API
   ================================================================ */

uint32_t fat32_read_file(fat32_context_t *ctx, fat32_entry_t *entry, uint8_t *buffer)
{
    if (!ctx || !entry || !buffer) return 0;
    uint32_t cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
    uint32_t read = 0;
    uint32_t size = entry->file_size;

    while (cluster >= 2 && cluster < 0x0FFFFFF0 && read < size) {
        uint32_t lba = fat32_cluster_to_lba(ctx, cluster);
        for (int i = 0; i < ctx->bpb.sectors_per_cluster && read < size; i++) {
            uint8_t sector_buf[512];
            if (!ata_read_sector_ext(ctx->drive_id, lba + i, sector_buf)) return read;
            uint32_t chunk = (size - read > 512) ? 512 : (size - read);
            memcpy(buffer + read, sector_buf, chunk);
            read += chunk;
        }
        cluster = fat32_get_fat_entry(ctx, cluster);
    }
    return read;
}

bool fat32_find_file(fat32_context_t *ctx, const char *name, fat32_entry_t *out)
{
    if (!ctx || !name) return false;
    struct finddir_ctx fc;
    fc.target_name = name;
    fc.found = false;
    fat32_iterate_dir(ctx, ctx->bpb.root_cluster, finddir_callback, &fc);
    if (fc.found && out) *out = fc.result;
    return fc.found;
}

int fat32_create_file(fat32_context_t *ctx, const char *filename)
{
    if (!ctx || !filename) return -1;
    uint32_t new_cluster = fat32_alloc_cluster(ctx);
    if (new_cluster == 0) return -1;

    fat32_entry_t new_entry;
    memset(&new_entry, 0, sizeof(fat32_entry_t));
    memset(new_entry.filename, ' ', 8);
    memset(new_entry.ext, ' ', 3);

    const char *dot = strrchr(filename, '.');
    int name_len = dot ? (int)(dot - filename) : (int)strlen(filename);
    if (name_len > 8) name_len = 8;
    for (int i = 0; i < name_len; i++) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        new_entry.filename[i] = c;
    }
    if (dot) {
        int ext_len = (int)strlen(dot + 1);
        if (ext_len > 3) ext_len = 3;
        for (int i = 0; i < ext_len; i++) {
            char c = dot[1+i];
            if (c >= 'a' && c <= 'z') c -= 32;
            new_entry.ext[i] = c;
        }
    }

    new_entry.attributes = ATTR_ARCHIVE;
    new_entry.first_cluster_low = new_cluster & 0xFFFF;
    new_entry.first_cluster_high = (new_cluster >> 16) & 0xFFFF;

    uint32_t cluster = ctx->bpb.root_cluster;
    uint8_t buf[512];
    while (cluster >= 2 && cluster < 0x0FFFFFF0) {
        uint32_t lba = fat32_cluster_to_lba(ctx, cluster);
        for (int s = 0; s < ctx->bpb.sectors_per_cluster; s++) {
            if (!ata_read_sector_ext(ctx->drive_id, lba + s, buf)) return -1;
            fat32_entry_t *entries = (fat32_entry_t *)buf;
            for (int i = 0; i < 512/32; i++) {
                if ((uint8_t)entries[i].filename[0] == 0x00 || (uint8_t)entries[i].filename[0] == 0xE5) {
                    entries[i] = new_entry;
                    ata_write_sector_ext(ctx->drive_id, lba + s, buf);
                    return 0;
                }
            }
        }
        cluster = fat32_get_fat_entry(ctx, cluster);
    }
    return -1;
}

int fat32_write_file(fat32_context_t *ctx, const char *filename, uint8_t *data, uint32_t size)
{
    if (!ctx || !filename || !data) return -1;
    fat32_entry_t entry;
    if (!fat32_find_file(ctx, filename, &entry)) return -1;

    uint32_t cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    uint32_t written = 0;
    while (written < size) {
        uint32_t lba = fat32_cluster_to_lba(ctx, cluster);
        for (int i = 0; i < ctx->bpb.sectors_per_cluster && written < size; i++) {
            uint8_t sector_buf[512];
            memset(sector_buf, 0, 512);
            uint32_t chunk = (size - written > 512) ? 512 : (size - written);
            memcpy(sector_buf, data + written, chunk);
            ata_write_sector_ext(ctx->drive_id, lba + i, sector_buf);
            written += chunk;
        }
        if (written < size) {
            uint32_t next = fat32_get_fat_entry(ctx, cluster);
            if (next >= 0x0FFFFFF8) {
                next = fat32_alloc_cluster(ctx);
                if (next == 0) return -1;
                fat32_set_fat_entry(ctx, cluster, next);
            }
            cluster = next;
        }
    }
    /* Simple size update omitted for brevity, but could be added here */
    return 0;
}

void fat32_get_stats(fat32_context_t *ctx, uint32_t *total, uint32_t *free)
{
    if (total) *total = ctx->total_sectors;
    if (free) *free = ctx->total_sectors; // Scanning omitted, return total as free for now
}

int fat32_mkdir(fat32_context_t *ctx, const char *name)
{
    if (!ctx || !name) return -1;
    
    // 1. Allocate a cluster for the directory
    uint32_t new_cluster = fat32_alloc_cluster(ctx);
    if (new_cluster == 0) return -1;

    // 2. Initialize the directory with . and ..
    uint8_t buffer[512];
    memset(buffer, 0, 512);
    
    fat32_entry_t *dot = (fat32_entry_t *)&buffer[0];
    memcpy(dot->filename, ".       ", 8);
    memcpy(dot->ext, "   ", 3);
    dot->attributes = ATTR_DIRECTORY;
    dot->first_cluster_low = new_cluster & 0xFFFF;
    dot->first_cluster_high = (new_cluster >> 16) & 0xFFFF;

    fat32_entry_t *dotdot = (fat32_entry_t *)&buffer[sizeof(fat32_entry_t)];
    memcpy(dotdot->filename, "..      ", 8);
    memcpy(dotdot->ext, "   ", 3);
    dotdot->attributes = ATTR_DIRECTORY;
    // Parent is root cluster if we're creating in root, etc.
    // Simplifying: Assume parent is root or 0 for now.
    dotdot->first_cluster_low = 0; 
    dotdot->first_cluster_high = 0;

    ata_write_sector_ext(ctx->drive_id, fat32_cluster_to_lba(ctx, new_cluster), buffer);

    // 3. Register in parent dir (similar to fat32_create_file)
    fat32_entry_t new_entry;
    memset(&new_entry, 0, sizeof(fat32_entry_t));
    memset(new_entry.filename, ' ', 8);
    memset(new_entry.ext, ' ', 3);

    int name_part_len = 0;
    while (name[name_part_len] && name[name_part_len] != '.' && name_part_len < 8) {
        char c = name[name_part_len];
        if (c >= 'a' && c <= 'z') c -= 32;
        new_entry.filename[name_part_len] = c;
        name_part_len++;
    }
    
    new_entry.attributes = ATTR_DIRECTORY;
    new_entry.first_cluster_low = new_cluster & 0xFFFF;
    new_entry.first_cluster_high = (new_cluster >> 16) & 0xFFFF;

    uint32_t cluster = ctx->bpb.root_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF0) {
        uint32_t lba = fat32_cluster_to_lba(ctx, cluster);
        for (int s = 0; s < ctx->bpb.sectors_per_cluster; s++) {
            if (!ata_read_sector_ext(ctx->drive_id, lba + s, buffer)) return -1;
            fat32_entry_t *entries = (fat32_entry_t *)buffer;
            for (int i = 0; i < 512/32; i++) {
                if ((uint8_t)entries[i].filename[0] == 0x00 || (uint8_t)entries[i].filename[0] == 0xE5) {
                    entries[i] = new_entry;
                    ata_write_sector_ext(ctx->drive_id, lba + s, buffer);
                    return 0;
                }
            }
        }
        cluster = fat32_get_fat_entry(ctx, cluster);
    }
    return -1;
}

int fat32_delete_file(fat32_context_t *ctx, const char *name) {
    (void)ctx; (void)name;
    serial_log("FAT32: delete_file called (STUB)");
    return -1;
}

} /* extern "C" */
