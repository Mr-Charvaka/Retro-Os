#include "fat32.h"
#include "../include/dirent.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "../kernel/heap.h"
#include "../kernel/memory.h"
#include "block.h"
#include "serial.h"

#include "../kernel/process.h"

// Per-drive context lock replaces global fat32_lock
 
extern "C" {
extern fat32_context_t *system_fat32_ctx;
// current_process macro from process.h will be used

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

    if (ctx->cached_fat_sector != fat_sector) {
        if (!block_read(ctx->drive_id, (uint64_t)fat_sector, 1, ctx->fat_cache)) {
            serial_log("FAT32: ERROR reading FAT sector via Block Layer");
            return 0x0FFFFFFF;
        }
        ctx->cached_fat_sector = fat_sector;
    }

    uint32_t val = *(uint32_t *)(ctx->fat_cache + entry_offset);
    return val & 0x0FFFFFFF;
}

/**
 * Write a FAT entry to both FAT1 and FAT2.
 */
static void fat32_set_fat_entry(fat32_context_t *ctx, uint32_t cluster, uint32_t value)
{
    // Invalidate cache if we are writing to the currently cached sector
    uint32_t fat_byte_offset = cluster * 4;
    uint32_t sector_offset   = fat_byte_offset / 512;
    uint32_t fat1_sector = ctx->fat_start_sector + sector_offset;
    if (ctx->cached_fat_sector == fat1_sector) {
        ctx->cached_fat_sector = 0xFFFFFFFF;
    }

    uint32_t entry_offset    = fat_byte_offset % 512;
    uint8_t buffer[512];

    /* Write to FAT1 */
    if (block_read(ctx->drive_id, (uint64_t)fat1_sector, 1, buffer)) {
        *(uint32_t *)(buffer + entry_offset) = value & 0x0FFFFFFF;
        block_write(ctx->drive_id, (uint64_t)fat1_sector, 1, buffer);
    }

    /* Write to FAT2 (mirror) */
    uint32_t fat2_sector = ctx->fat_start_sector
                         + ctx->bpb.sectors_per_fat_32
                         + sector_offset;
    if (block_read(ctx->drive_id, (uint64_t)fat2_sector, 1, buffer)) {
        *(uint32_t *)(buffer + entry_offset) = value & 0x0FFFFFFF;
        block_write(ctx->drive_id, (uint64_t)fat2_sector, 1, buffer);
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
                block_write(ctx->drive_id, (uint64_t)(lba + i), 1, zero);
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
/**
 * Reconstructs a null-terminated UTF-8 string from multiple LFN entries.
 */
static void fat32_lfn_to_string(char *dest, fat_lfn_entry_t *lfn_entries, int count) {
    int k = 0;
    // LFN entries are stored in REVERSE order OR sequence order depends on 'order' byte
    // Actually, LFN entries are scanned as they appear. 
    // This is a simplified version that just maps the current entry into the dest.
    for (int i = 0; i < count; i++) {
        fat_lfn_entry_t *e = &lfn_entries[i];
        // We only take the low byte of the UTF-16 character (ASCII) for now
        for (int j = 0; j < 5; j++) if ((e->first_5[j] & 0xFF) && k < 255) dest[k++] = (char)(e->first_5[j] & 0xFF);
        for (int j = 0; j < 6; j++) if ((e->next_6[j] & 0xFF) && k < 255)  dest[k++] = (char)(e->next_6[j] & 0xFF);
        for (int j = 0; j < 2; j++) if ((e->last_2[j] & 0xFF)  && k < 255) dest[k++] = (char)(e->last_2[j] & 0xFF);
    }
    dest[k] = '\0';
}

static bool fat32_name_match(const char *a, const char *b)
{
    if (!a || !b) return false;
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
                        const char      *name, // Full name (LFN or 8.3)
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
    
    fat_lfn_entry_t lfn_entries[20];
    int lfn_count = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF0) {
        uint32_t lba = fat32_cluster_to_lba(ctx, cluster);

        for (int s = 0; s < ctx->bpb.sectors_per_cluster; s++) {
            if (!block_read(ctx->drive_id, (uint64_t)(lba + s), 1, buffer)) {
                serial_log("FAT32: ERROR reading dir sector");
                return -1;
            }

            fat32_entry_t *entries = (fat32_entry_t *)buffer;
            int entries_per_sector = 512 / sizeof(fat32_entry_t);

            for (int i = 0; i < entries_per_sector; i++) {
                fat32_entry_t *e = &entries[i];

                if ((uint8_t)e->filename[0] == 0x00) return 0;
                if ((uint8_t)e->filename[0] == 0xE5) {
                    lfn_count = 0;
                    continue;
                }
                
                if (e->attributes == ATTR_LFN) {
                    if (lfn_count < 20) {
                        memcpy(&lfn_entries[lfn_count++], e, sizeof(fat_lfn_entry_t));
                    }
                    continue;
                }

                if (e->attributes & ATTR_VOLUME_ID) {
                    lfn_count = 0;
                    continue;
                }

                char final_name[256];
                if (lfn_count > 0) {
                    char temp[256];
                    int t_idx = 0;
                    // LFN entries in disk are stored before SFN, in REVERSE sequence.
                    // The first LFN entry seen here is actually the last part of the name.
                    // So we must iterate backwards through our collected entries.
                    for (int n = lfn_count - 1; n >= 0; n--) {
                        fat_lfn_entry_t *le = &lfn_entries[n];
                        for (int j = 0; j < 5; j++) if (le->first_5[j] && le->first_5[j] != 0xFFFF) temp[t_idx++] = le->first_5[j] & 0xFF;
                        for (int j = 0; j < 6; j++) if (le->next_6[j] && le->next_6[j] != 0xFFFF)   temp[t_idx++] = le->next_6[j] & 0xFF;
                        for (int j = 0; j < 2; j++) if (le->last_2[j] && le->last_2[j] != 0xFFFF)   temp[t_idx++] = le->last_2[j] & 0xFF;
                    }
                    temp[t_idx] = '\0';
                    strcpy(final_name, temp);
                } else {
                    fat32_83_to_name(final_name, e->filename, e->ext);
                }
                
                lfn_count = 0; 

                int res = callback(ctx, e, final_name, lba + s, i * sizeof(fat32_entry_t), userdata);
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

    if (!block_read(drive_id, (uint64_t)start_lba, 1, sector0)) {
        serial_log("FAT32: ERROR could not read BPB sector");
        return nullptr;
    }

    // Diagnostics
    serial_log("FAT32: BPB Diagnostic for LBA ");
    serial_log_hex("", start_lba);
    uint16_t bps = *(uint16_t*)(sector0 + 11);
    uint8_t spc = sector0[13];
    uint16_t res = *(uint16_t*)(sector0 + 14);
    serial_log("  BytesPerSector: "); serial_log_hex("", bps);
    serial_log("  SectorsPerCluster: "); serial_log_hex("", spc);
    serial_log("  ReservedSectors: "); serial_log_hex("", res);

    if (bps != 512) {
        serial_log("FAT32: ERROR invalid BPB (BytesPerSector != 512)");
        return nullptr;
    }

    fat32_context_t *ctx = (fat32_context_t *)kmalloc(sizeof(fat32_context_t));
    if (!ctx) return nullptr;
    memset(ctx, 0, sizeof(fat32_context_t));
    ctx->cached_fat_sector = 0xFFFFFFFF;

    ctx->drive_id        = drive_id;
    ctx->partition_start = start_lba;
    memcpy(&ctx->bpb, sector0, sizeof(fat32_bpb_t));

    ctx->fat_start_sector  = start_lba + ctx->bpb.reserved_sectors;
    ctx->data_start_sector = ctx->fat_start_sector
                           + (ctx->bpb.fats_count * ctx->bpb.sectors_per_fat_32);

    ctx->total_sectors = (ctx->bpb.total_sectors_16 != 0)
                       ? ctx->bpb.total_sectors_16
                       : ctx->bpb.total_sectors_32;

    // Rev Q: Initialize per-drive lock
    ctx->lock = 0;

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
        spinlock_lock(&ctx->lock); // [Lock Per Drive]
        
        uint32_t cluster_start = file_pos;
        uint32_t cluster_end = file_pos + cluster_bytes;

        // Optimized: If we need a whole cluster and it's aligned, do it in one go
        if (cluster_start >= offset && cluster_end <= offset + size && (cluster_start - offset) % 512 == 0) {
            uint32_t lba = fat32_cluster_to_lba(ctx, cluster);
            uint32_t dest_off = cluster_start - offset;
            if (block_read(ctx->drive_id, (uint64_t)lba, ctx->bpb.sectors_per_cluster, buffer + dest_off)) {
                bytes_read += cluster_bytes;
                file_pos += cluster_bytes;
            } else {
                spinlock_unlock(&ctx->lock);
                return bytes_read;
            }
        } 
        else if (cluster_end > offset && cluster_start < offset + size) {
            // Partial cluster read
            uint32_t lba = fat32_cluster_to_lba(ctx, cluster);
            for (int s = 0; s < ctx->bpb.sectors_per_cluster && bytes_read < size; s++) {
                uint32_t sector_start = file_pos;
                uint32_t sector_end = file_pos + 512;

                if (sector_end > offset && sector_start < offset + size) {
                    uint8_t sector_buf[512];
                    if (!block_read(ctx->drive_id, (uint64_t)(lba + s), 1, sector_buf)) {
                        spinlock_unlock(&ctx->lock);
                        return bytes_read;
                    }

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
        
        uint32_t next_cluster = fat32_get_fat_entry(ctx, cluster);
        cluster = next_cluster;
        
        spinlock_unlock(&ctx->lock); // [Unlock Per Drive]
        
        // Back-off to ensure GUI core isn't starved
        for (volatile int i = 0; i < 2000; i++) { asm volatile("pause"); }
        kernel_yield(); 
    }
    return bytes_read;
}

struct readdir_ctx {
    uint32_t target_index;
    uint32_t current_index;
    struct dirent de;
    bool found;
};

static int readdir_callback(fat32_context_t *ctx, fat32_entry_t *entry, const char *name, uint32_t sector, uint32_t offset, void *userdata)
{
    (void)ctx; (void)sector; (void)offset;
    struct readdir_ctx *rc = (struct readdir_ctx *)userdata;
    if (rc->current_index == rc->target_index) {
        strncpy(rc->de.d_name, name, 255);
        rc->de.d_type = (entry->attributes & ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
        rc->de.d_ino = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
        rc->found = true;
        return 1;
    }
    rc->current_index++;
    return 0;
}

static struct dirent g_readdir_de;
static struct dirent *fat32_readdir_vfs(vfs_node_t *node, uint32_t index)
{
    if (!node || !node->device) return nullptr;
    fat32_context_t *ctx = (fat32_context_t *)node->device;
    uint32_t dir_cluster = (uint32_t)(uintptr_t)node->impl;

    struct readdir_ctx rc;
    rc.target_index = index;
    rc.current_index = 0;
    rc.found = false;
    memset(&rc.de, 0, sizeof(struct dirent));

    spinlock_lock(&ctx->lock);
    fat32_iterate_dir(ctx, dir_cluster, readdir_callback, &rc);
    spinlock_unlock(&ctx->lock);

    if (rc.found) {
        g_readdir_de = rc.de;
        return &g_readdir_de;
    }
    return nullptr;
}

struct finddir_ctx {
    const char *target_name;
    fat32_entry_t result;
    bool found;
};

static int finddir_callback(fat32_context_t *ctx, fat32_entry_t *entry, const char *name, uint32_t sector, uint32_t offset, void *userdata)
{
    (void)ctx; (void)sector; (void)offset;
    struct finddir_ctx *fc = (struct finddir_ctx *)userdata;
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

    spinlock_lock(&ctx->lock);
    fat32_iterate_dir(ctx, dir_cluster, finddir_callback, &fc);
    spinlock_unlock(&ctx->lock);

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
    .mount = nullptr,
    .lookup = fat32_finddir_vfs,
    .create = [](vfs_node_t *parent, const char *name, int type) -> int {
        fat32_context_t *ctx = (fat32_context_t *)parent->device;
        if (type == VFS_DIRECTORY) return fat32_mkdir(ctx, name);
        return fat32_create_file(ctx, name);
    },
    .read = nullptr,
    .write = nullptr,
    .close = nullptr,
    .readdir = fat32_readdir_vfs,
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
    uint32_t total_size = entry->file_size;
    uint32_t bytes_read = 0;

    uint32_t sectors_per_cluster = ctx->bpb.sectors_per_cluster;

    while (cluster >= 2 && cluster < 0x0FFFFFF0 && bytes_read < total_size) {
        // Step 1: Identify contiguous run of clusters
        uint32_t run_clusters = 1;
        uint32_t current = cluster;
        
        // Rev AG: Lookahead up to 256 sectors (our DMA limit)
        while (run_clusters * sectors_per_cluster < 256) {
            uint32_t next = fat32_get_fat_entry(ctx, current);
            if (next == current + 1) {
                run_clusters++;
                current = next;
            } else {
                break;
            }
        }

        uint32_t run_bytes = run_clusters * sectors_per_cluster * 512;
        if (bytes_read + run_bytes > total_size) run_bytes = total_size - bytes_read;

        uint32_t start_lba = fat32_cluster_to_lba(ctx, cluster);
        uint32_t run_sectors = (run_bytes + 511) / 512;

        // Step 2: Direct High-Speed DMA (No memcpy)
        if (!block_read(ctx->drive_id, (uint64_t)start_lba, run_sectors, buffer + bytes_read)) {
            serial_log("FAT32: Sonic Read Failed!");
            return bytes_read;
        }

        bytes_read += run_bytes;
        cluster = fat32_get_fat_entry(ctx, current); // Grab the pointer AFTER the run
    }
    return bytes_read;
}

bool fat32_find_file(fat32_context_t *ctx, const char *name, fat32_entry_t *out)
{
    if (!ctx || !name) return false;
    struct finddir_ctx fc;
    fc.target_name = name;
    fc.found = false;
    spinlock_lock(&ctx->lock);
    fat32_iterate_dir(ctx, ctx->bpb.root_cluster, finddir_callback, &fc);
    spinlock_unlock(&ctx->lock);
    if (fc.found && out) *out = fc.result;
    return fc.found;
}

int fat32_create_file(fat32_context_t *ctx, const char *filename)
{
    if (!ctx || !filename) return -1;
    
    spinlock_lock(&ctx->lock);
    uint32_t new_cluster = fat32_alloc_cluster(ctx);
    if (new_cluster == 0) {
        spinlock_unlock(&ctx->lock);
        return -1;
    }

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
            if (!block_read(ctx->drive_id, (uint64_t)(lba + s), 1, buf)) {
                spinlock_unlock(&ctx->lock);
                return -1;
            }
            fat32_entry_t *entries = (fat32_entry_t *)buf;
            for (int i = 0; i < 512/32; i++) {
                if ((uint8_t)entries[i].filename[0] == 0x00 || (uint8_t)entries[i].filename[0] == 0xE5) {
                    entries[i] = new_entry;
                    block_write(ctx->drive_id, (uint64_t)(lba + s), 1, buf);
                    spinlock_unlock(&ctx->lock);
                    return 0;
                }
            }
        }
        cluster = fat32_get_fat_entry(ctx, cluster);
    }
    spinlock_unlock(&ctx->lock);
    return -1;
}

int fat32_write_file(fat32_context_t *ctx, const char *filename, uint8_t *data, uint32_t size)
{
    if (!ctx || !filename || !data) return -1;
    fat32_entry_t entry;
    if (!fat32_find_file(ctx, filename, &entry)) return -1;

    uint32_t cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    uint32_t written = 0;
    spinlock_lock(&ctx->lock);
    while (written < size) {
        uint32_t lba = fat32_cluster_to_lba(ctx, cluster);
        for (int i = 0; i < ctx->bpb.sectors_per_cluster && written < size; i++) {
            uint8_t sector_buf[512];
            memset(sector_buf, 0, 512);
            uint32_t chunk = (size - written > 512) ? 512 : (size - written);
            memcpy(sector_buf, data + written, chunk);
            if (!block_write(ctx->drive_id, (uint64_t)(lba + i), 1, sector_buf)) {
                spinlock_unlock(&ctx->lock);
                return -1;
            }
            written += chunk;
        }
        if (written < size) {
            uint32_t next = fat32_get_fat_entry(ctx, cluster);
            if (next >= 0x0FFFFFF8) {
                next = fat32_alloc_cluster(ctx);
                if (next == 0) {
                    spinlock_unlock(&ctx->lock);
                    return -1;
                }
                fat32_set_fat_entry(ctx, cluster, next);
            }
            cluster = next;
        }
    }
    spinlock_unlock(&ctx->lock);
    return 0;
}

void fat32_get_stats(fat32_context_t *ctx, uint32_t *total, uint32_t *free_bytes)
{
    if (total) *total = ctx->total_sectors * 512;
    if (free_bytes) *free_bytes = 0; // Scanning omitted
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
    dotdot->first_cluster_low = 0; 
    dotdot->first_cluster_high = 0;

    block_write(ctx->drive_id, (uint64_t)fat32_cluster_to_lba(ctx, new_cluster), 1, buffer);

    // 3. Register in parent dir
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
            if (!block_read(ctx->drive_id, (uint64_t)(lba + s), 1, buffer)) return -1;
            fat32_entry_t *entries = (fat32_entry_t *)buffer;
            for (int i = 0; i < 512/32; i++) {
                if ((uint8_t)entries[i].filename[0] == 0x00 || (uint8_t)entries[i].filename[0] == 0xE5) {
                    entries[i] = new_entry;
                    block_write(ctx->drive_id, (uint64_t)(lba + s), 1, buffer);
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
