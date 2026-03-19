#include "ata.h"
#include "../include/io.h"
#include "serial.h"

static uint8_t g_ata_selected_drive = 0;

static void ata_io_delay() {
  // 400ns delay required after drive/head changes on ATA.
  inb(ATA_STATUS);
  inb(ATA_STATUS);
  inb(ATA_STATUS);
  inb(ATA_STATUS);
}

static bool ata_wait_bsy() {
  for (uint32_t i = 0; i < 100000000; i++) {
    uint8_t st = inb(ATA_STATUS);
    if (st == 0x00 || st == 0xFF)
      return false;
    if ((st & ATA_SR_BSY) == 0)
      return true;
  }
  return false;
}

static bool ata_wait_drq() {
  for (uint32_t i = 0; i < 100000000; i++) {
    uint8_t st = inb(ATA_STATUS);
    if (st == 0x00 || st == 0xFF)
      return false;
    if (st & ATA_SR_ERR)
      return false;
    if (st & ATA_SR_DRQ)
      return true;
  }
  return false;
}

void ata_set_drive(uint8_t drive) { g_ata_selected_drive = drive ? 1 : 0; }

uint8_t ata_get_drive() { return g_ata_selected_drive; }

void ata_read_sector_drive(uint8_t drive, uint32_t lba, uint8_t *buffer) {
  asm volatile("cli");
  if (!ata_wait_bsy()) {
    for (int i = 0; i < 512; i++)
      buffer[i] = 0;
    asm volatile("sti");
    return;
  }

  outb(ATA_DRIVE_HEAD,
       0xE0 | ((drive ? 1 : 0) << 4) | ((lba >> 24) & 0x0F));
    ata_io_delay();
  outb(ATA_ERROR, 0x00);                             // Null byte
  outb(ATA_SECTOR_CNT, 1);                           // Sector Count = 1
  outb(ATA_LBA_LO, (uint8_t)lba);
  outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
  outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
  outb(ATA_COMMAND, ATA_CMD_READ_PIO);

  if (!ata_wait_bsy() || !ata_wait_drq()) {
    for (int i = 0; i < 512; i++)
      buffer[i] = 0;
    asm volatile("sti");
    return;
  }

  // Read 256 words (512 bytes)
  uint16_t *buf16 = (uint16_t *)buffer;
  for (int i = 0; i < 256; i++) {
    buf16[i] = inw(ATA_DATA);
  }
  asm volatile("sti");
}

void ata_write_sector_drive(uint8_t drive, uint32_t lba, uint8_t *buffer) {
  if (!ata_wait_bsy()) {
    return;
  }

  outb(ATA_DRIVE_HEAD,
       0xE0 | ((drive ? 1 : 0) << 4) | ((lba >> 24) & 0x0F));
    ata_io_delay();
  outb(ATA_ERROR, 0x00);
  outb(ATA_SECTOR_CNT, 1);
  outb(ATA_LBA_LO, (uint8_t)lba);
  outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
  outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
  outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);

  if (!ata_wait_bsy() || !ata_wait_drq()) {
    return;
  }

  // Write 256 words
  uint16_t *buf16 = (uint16_t *)buffer;
  for (int i = 0; i < 256; i++) {
    outw(ATA_DATA, buf16[i]);
  }

  // Flush Cache
  outb(ATA_COMMAND, 0xE7);
  ata_wait_bsy();
}

void ata_read_sector(uint32_t lba, uint8_t *buffer) {
  ata_read_sector_drive(g_ata_selected_drive, lba, buffer);
}

void ata_write_sector(uint32_t lba, uint8_t *buffer) {
  ata_write_sector_drive(g_ata_selected_drive, lba, buffer);
}
