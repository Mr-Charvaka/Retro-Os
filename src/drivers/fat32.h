#ifndef FAT32_H
#define FAT32_H

#include "../include/types.h"
#include "../include/vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

// FAT32 Boot Sector BPB
typedef struct {
    uint8_t jmp[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fats_count;
    uint16_t root_entries_count; // 0 for FAT32
    uint16_t total_sectors_16;   // 0 for FAT32
    uint8_t media_type;
    uint16_t sectors_per_fat_16; // 0 for FAT32
    uint16_t sectors_per_track;
    uint16_t heads_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    // FAT32 Extended BPB
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_num;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8]; // "FAT32   "
} __attribute__((packed)) fat32_bpb_t;

// FSInfo Structure
typedef struct {
    uint32_t lead_sig;       // 0x41615252
    uint8_t reserved[480];
    uint32_t struct_sig;     // 0x61417272
    uint32_t free_count;     // Last known free cluster count (0xFFFFFFFF if unknown)
    uint32_t next_free;      // Hint for next free cluster (0xFFFFFFFF if unknown)
    uint8_t reserved2[12];
    uint32_t trail_sig;      // 0xAA550000
} __attribute__((packed)) fat32_fsinfo_t;

// Directory Entry (same as FAT16, but with high cluster word used)
typedef struct {
    char filename[8];
    char ext[3];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high; // FAT32 only
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat32_entry_t;

// Attributes
#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define ATTR_LFN       0x0F

// Context for each FAT32 instance (Drive 0, Drive 1, etc.)
typedef struct {
    uint8_t drive_id;
    fat32_bpb_t bpb;
    uint32_t fat_start_sector;
    uint32_t data_start_sector;
    uint32_t total_sectors;
    uint32_t partition_start;
} fat32_context_t;

// Initialize a specific drive as FAT32
fat32_context_t *fat32_mount(uint8_t drive_id, uint32_t start_lba);

// VFS Integration for a specific context
vfs_node_t *fat32_vfs_init(fat32_context_t *ctx);

// Internal Storage APIs for a specific context
uint32_t fat32_read_file(fat32_context_t *ctx, fat32_entry_t *entry, uint8_t *buffer);
int fat32_create_file(fat32_context_t *ctx, const char *filename);
int fat32_write_file(fat32_context_t *ctx, const char *filename, uint8_t *data, uint32_t size);
int fat32_mkdir(fat32_context_t *ctx, const char *name);
int fat32_delete_file(fat32_context_t *ctx, const char *name);
bool fat32_find_file(fat32_context_t *ctx, const char *name, fat32_entry_t *out);
void fat32_get_stats(fat32_context_t *ctx, uint32_t *total_sectors, uint32_t *free_sectors);

#ifdef __cplusplus
}
#endif

#endif // FAT32_H
