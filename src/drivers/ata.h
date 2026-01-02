#ifndef ATA_H
#define ATA_H

#include "../include/types.h"

// ATA Bus I/O Ports (Primary Bus)
#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECTOR_CNT 0x1F2
#define ATA_LBA_LO 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HI 0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7

// Status Flags
#define ATA_SR_BSY 0x80 // Busy
#define ATA_SR_DRQ 0x08 // Data Request ready
#define ATA_SR_ERR 0x01 // Error

// Commands
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_WRITE_PIO 0x30

// Functions
void ata_read_sector(uint32_t lba, uint8_t *buffer);
void ata_write_sector(uint32_t lba, uint8_t *buffer);

#endif
