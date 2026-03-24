#include "ata.h"
#include "../include/io.h"
#include "serial.h"

static inline uint32_t save_flags_and_cli() {
  uint32_t flags;
  asm volatile("pushfl; popl %0; cli" : "=r"(flags));
  return flags;
}

static inline void restore_flags(uint32_t flags) {
  asm volatile("pushl %0; popfl" : : "r"(flags));
}

bool ata_wait_bsy() {
  int timeout = 1000000; // Increased to 1M for safety
  while (timeout > 0) {
    uint8_t status = inb(ATA_STATUS);
    if (status == 0xFF) return false; // Floating bus / error
    if (!(status & ATA_SR_BSY)) return true;
    timeout--;
  }
  return false;
}

bool ata_wait_drq() {
  int timeout = 1000000; // Increased to 1M
  while (timeout > 0) {
    uint8_t status = inb(ATA_STATUS);
    if (status & ATA_SR_DRQ) return true;
    if (status & ATA_SR_ERR) return false;
    timeout--;
  }
  return false;
}

bool ata_read_sector_ext(uint8_t drive, uint32_t lba, uint8_t *buffer) {
  uint32_t flags = save_flags_and_cli();
  
  if (!ata_wait_bsy()) {
    restore_flags(flags);
    return false;
  }

  uint8_t head = (drive == 0) ? 0xE0 : 0xF0;
  outb(ATA_DRIVE_HEAD, head | ((lba >> 24) & 0x0F));
  outb(ATA_ERROR, 0x00);
  outb(ATA_SECTOR_CNT, 1);
  outb(ATA_LBA_LO, (uint8_t)lba);
  outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
  outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
  outb(ATA_COMMAND, ATA_CMD_READ_PIO);

  if (!ata_wait_bsy() || !ata_wait_drq()) {
    restore_flags(flags);
    return false;
  }

  // Read 256 words
  uint16_t *buf16 = (uint16_t *)buffer;
  for (int i = 0; i < 256; i++) {
    buf16[i] = inw(ATA_DATA);
  }
  restore_flags(flags);
  return true;
}

void ata_read_sector(uint32_t lba, uint8_t *buffer) {
  ata_read_sector_ext(0, lba, buffer);
}

bool ata_write_sector_ext(uint8_t drive, uint32_t lba, uint8_t *buffer) {
  if (!ata_wait_bsy())
    return false;

  uint8_t head = (drive == 0) ? 0xE0 : 0xF0;
  outb(ATA_DRIVE_HEAD, head | ((lba >> 24) & 0x0F));
  outb(ATA_ERROR, 0x00);
  outb(ATA_SECTOR_CNT, 1);
  outb(ATA_LBA_LO, (uint8_t)lba);
  outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
  outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
  outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);

  if (!ata_wait_bsy() || !ata_wait_drq())
    return false;

  // Write 256 words
  uint16_t *buf16 = (uint16_t *)buffer;
  for (int i = 0; i < 256; i++) {
    outw(ATA_DATA, buf16[i]);
  }

  // Flush Cache
  outb(ATA_COMMAND, 0xE7);
  if (!ata_wait_bsy())
    return false;
  return true;
}

void ata_write_sector(uint32_t lba, uint8_t *buffer) {
  ata_write_sector_ext(0, lba, buffer);
}
