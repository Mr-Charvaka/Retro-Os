#ifndef NTFS_H
#define NTFS_H

#include "../include/types.h"
#include "../include/vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

// NTFS VBR BPB
typedef struct {
    uint8_t jmp[3];
    char oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t zero1[3];
    uint16_t unused1;
    uint8_t media_type;
    uint16_t unused2;
    uint16_t sectors_per_track;
    uint16_t heads_count;
    uint32_t hidden_sectors;
    uint32_t unused3;
    uint32_t dragon_signature;
    uint64_t total_sectors;
    uint64_t mft_cluster;
    uint64_t mft_mirror_cluster;
    int8_t clusters_per_mft_record;
    uint8_t unused4[3];
    int8_t clusters_per_index_record;
    uint8_t unused5[3];
    uint64_t volume_serial;
} __attribute__((packed)) ntfs_bpb_t;

// MFT Header
typedef struct {
    char magic[4]; // "FILE"
    uint16_t update_seq_offset;
    uint16_t update_seq_size;
    uint64_t log_seq_num;
    uint16_t sequence_num;
    uint16_t hard_link_count;
    uint16_t first_attribute_offset;
    uint16_t flags; // 1 = In use, 2 = Directory
    uint32_t used_size;
    uint32_t allocated_size;
    uint64_t base_record;
    uint16_t next_attribute_id;
} __attribute__((packed)) ntfs_mft_record_t;

// Attribute Common Header
typedef struct {
    uint32_t type;
    uint32_t length;
    uint8_t non_resident;
    uint8_t name_length;
    uint16_t name_offset;
    uint16_t flags;
    uint16_t attribute_id;
} __attribute__((packed)) ntfs_attr_header_t;

// Resident Attribute
typedef struct {
    ntfs_attr_header_t header;
    uint32_t content_size;
    uint16_t content_offset;
    uint8_t indexed_flag;
    uint8_t padding;
} __attribute__((packed)) ntfs_attr_resident_t;

// Non-Resident Attribute
typedef struct {
    ntfs_attr_header_t header;
    uint64_t starting_vcn;
    uint64_t ending_vcn;
    uint16_t run_list_offset;
    uint16_t compression_unit_size;
    uint32_t padding;
    uint64_t allocated_size;
    uint64_t real_size;
    uint64_t initialized_size;
} __attribute__((packed)) ntfs_attr_nonresident_t;

// $FILE_NAME Attribute
typedef struct {
    uint64_t parent_directory;
    uint64_t creation_time;
    uint64_t alteration_time;
    uint64_t mft_changed_time;
    uint64_t read_time;
    uint64_t allocated_size;
    uint64_t real_size;
    uint32_t flags;
    uint32_t reparse_tag;
    uint8_t name_length;
    uint8_t name_namespace;
    uint16_t name[1]; // Unicode (UTF-16LE)
} __attribute__((packed)) ntfs_attr_filename_t;

// Attribute Types
#define NTFS_TYPE_STANDARD_INFO 0x10
#define NTFS_TYPE_ATTR_LIST     0x20
#define NTFS_TYPE_FILE_NAME     0x30
#define NTFS_TYPE_DATA          0x80
#define NTFS_TYPE_INDEX_ROOT    0x90
#define NTFS_TYPE_INDEX_ALLOC   0xA0
#define NTFS_TYPE_BITMAP        0xB0

// MFT Standard Indices
#define NTFS_MFT_FILE_ROOT 5

// Data Run Structure
typedef struct {
    uint64_t cluster_offset;
    uint64_t cluster_count;
} ntfs_run_t;

// Internal context
typedef struct {
    uint8_t drive_id;
    uint32_t partition_start_lba;
    ntfs_bpb_t bpb;
    uint32_t bytes_per_cluster;
    uint32_t bytes_per_mft_record;
    uint32_t mft_start_lba;
} ntfs_context_t;

// APIs
ntfs_context_t *ntfs_mount(uint8_t drive_id, uint32_t partition_start_lba);
vfs_node_t *ntfs_vfs_init(ntfs_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
