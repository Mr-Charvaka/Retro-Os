#ifndef BLOCK_H
#define BLOCK_H

#include "../include/types.h"

typedef enum {
    BLOCK_DRIVER_NONE = 0,
    BLOCK_DRIVER_ATA_PIO,   // Kept for enum stability — never used
    BLOCK_DRIVER_AHCI,
    BLOCK_DRIVER_NVME
} block_driver_type_t;

typedef struct {
    block_driver_type_t type;
    uint8_t             drive_id;
    uint64_t            total_sectors;
    uint32_t            sector_size;
    const char         *name;
} block_device_t;

#define MAX_BLOCK_DEVICES 8

#ifdef __cplusplus
extern "C" {
#endif

void block_init(void);
bool block_read(uint8_t dev_id, uint64_t lba, uint32_t count, void *buffer);
bool block_write(uint8_t dev_id, uint64_t lba, uint32_t count, const void *buffer);
block_device_t *block_get_device(uint8_t dev_id);
int block_get_device_count(void);
uint8_t block_get_boot_device(void);
void block_print_devices(void);

#ifdef __cplusplus
}
#endif

#endif // BLOCK_H
