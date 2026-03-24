#ifndef MBR_H
#define MBR_H

#include "../include/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Flagship Partition Entry (Standard compliant)
typedef struct {
    uint8_t boot_indicator;  // 0x80 = Active/Bootable
    uint8_t start_chs[3];
    uint8_t partition_type;
    uint8_t end_chs[3];
    uint32_t start_lba;
    uint32_t sector_count;
} __attribute__((packed)) mbr_partition_t;

typedef struct {
    uint8_t boot_code[446];
    mbr_partition_t partitions[4];
    uint16_t signature; // 0xAA55
} __attribute__((packed)) mbr_t;

// Metadata for discovered partitions
typedef struct {
    uint32_t disk_id;
    uint32_t part_id;
    uint32_t start_lba;
    uint32_t sector_count;
    uint8_t type;
    bool active;
} partition_info_t;

// Max partitions we'll track (4 primary + up to 12 extended)
#define MAX_PARTITIONS_PER_DISK 16

// Flagship Partition APIs
int mbr_enumerate_partitions(uint8_t drive_id, partition_info_t *list, int max_count);
uint32_t mbr_find_first_of_type(uint8_t drive_id, uint8_t type);

// Standard Partition Types
#define PART_TYPE_EMPTY        0x00
#define PART_TYPE_FAT12        0x01
#define PART_TYPE_FAT16        0x06
#define PART_TYPE_NTFS         0x07
#define PART_TYPE_FAT32        0x0B
#define PART_TYPE_FAT32_LBA    0x0C
#define PART_TYPE_EXTENDED     0x05
#define PART_TYPE_EXTENDED_LBA 0x0F

#ifdef __cplusplus
}
#endif

#endif
