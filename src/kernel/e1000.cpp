// kernel_e1000.cpp
// Intel e1000 Network Driver (QEMU Friendly)
// 32-bit x86, MMIO, DMA-capable
// Retro-OS Networking Phase 1

#include "../drivers/pci.h"
#include "paging.h"
#include "vm.h"
#include <stddef.h>
#include <stdint.h>

// =======================================================
// BRIDGE TO RETRO-OS
// =======================================================

extern "C" uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func,
                             uint8_t offset) {
  return pci_read_config(bus, slot, func, offset);
}

extern "C" void pci_write(uint8_t bus, uint8_t slot, uint8_t func,
                          uint8_t offset, uint32_t value) {
  pci_write_config(bus, slot, func, offset, value);
}

extern "C" void *map_mmio(uint32_t phys_addr, size_t size) {
  uint32_t pages = (size + 4095) / 4096;
  for (uint32_t i = 0; i < pages; i++) {
    // Map identity for kernel MMIO access
    paging_map(phys_addr + (i * 4096), phys_addr + (i * 4096), 3);
  }
  return (void *)phys_addr;
}

extern "C" uint32_t virt_to_phys(void *addr) {
  if ((uint32_t)addr >= KERNEL_VIRTUAL_BASE)
    return VIRT_TO_PHYS(addr);
  return vm_get_phys((uint32_t)addr);
}

// =======================================================
// e1000 REGISTERS
// =======================================================

#define E1000_CTRL 0x0000
#define E1000_STATUS 0x0008

#define E1000_TCTL 0x0400
#define E1000_TIPG 0x0410
#define E1000_TDBAL 0x3800
#define E1000_TDLEN 0x3808
#define E1000_TDH 0x3810
#define E1000_TDT 0x3818

#define E1000_RCTL 0x0100
#define E1000_RDBAL 0x2800
#define E1000_RDLEN 0x2808
#define E1000_RDH 0x2810
#define E1000_RDT 0x2818

// =======================================================
// DESCRIPTORS
// =======================================================

#define RX_DESC_COUNT 32
#define TX_DESC_COUNT 32

struct rx_desc {
  uint64_t addr;
  uint16_t length;
  uint16_t checksum;
  uint8_t status;
  uint8_t errors;
  uint16_t special;
} __attribute__((packed));

struct tx_desc {
  uint64_t addr;
  uint16_t length;
  uint8_t cso;
  uint8_t cmd;
  uint8_t status;
  uint8_t css;
  uint16_t special;
} __attribute__((packed));

// =======================================================
// DRIVER STATE
// =======================================================

static volatile uint32_t *e1000_mmio;

// CRITICAL: All descriptor rings and buffers MUST be 16-byte aligned for DMA
static rx_desc rx_ring[RX_DESC_COUNT] __attribute__((aligned(16)));
static tx_desc tx_ring[TX_DESC_COUNT] __attribute__((aligned(16)));

static uint8_t rx_buffers[RX_DESC_COUNT][2048] __attribute__((aligned(16)));
static uint8_t tx_buffers[TX_DESC_COUNT][2048] __attribute__((aligned(16)));

static uint32_t rx_tail = 0;
static uint32_t tx_tail = 0;

// =======================================================
// MMIO ACCESS
// =======================================================

static inline void e1000_write(uint32_t reg, uint32_t val) {
  e1000_mmio[reg / 4] = val;
}

static inline uint32_t e1000_read(uint32_t reg) { return e1000_mmio[reg / 4]; }

// =======================================================
// INITIALIZATION
// =======================================================

extern "C" void e1000_init(uint8_t bus, uint8_t slot, uint8_t func) {
  extern void serial_log(const char *);
  extern void serial_log_hex(const char *, uint32_t);

  serial_log("e1000: Initializing...");

  // Enable bus mastering
  uint32_t cmd = pci_read(bus, slot, func, 0x04);
  cmd |= (1 << 2) | (1 << 1); // Bus Master + Memory Space
  pci_write(bus, slot, func, 0x04, cmd);

  // Read BAR0
  uint32_t bar0 = pci_read(bus, slot, func, 0x10);
  uint32_t mmio_phys = bar0 & ~0xF;
  serial_log_hex("e1000: MMIO at ", mmio_phys);

  e1000_mmio = (uint32_t *)map_mmio(mmio_phys, 0x20000);

  // Reset
  e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | (1 << 26));
  for (volatile int i = 0; i < 1000000; i++)
    ;

  // Clear multicast table
  for (int i = 0; i < 128; i++) {
    e1000_write(0x5200 + (i * 4), 0);
  }

  // Setup RX ring
  for (int i = 0; i < RX_DESC_COUNT; i++) {
    rx_ring[i].addr = virt_to_phys(rx_buffers[i]);
    rx_ring[i].status = 0;
  }

  uint32_t rx_ring_phys = virt_to_phys((void *)rx_ring);
  serial_log_hex("e1000: RX Ring Phys: ", rx_ring_phys);
  serial_log_hex("e1000: RX Buf[0] Phys: ", (uint32_t)rx_ring[0].addr);

  e1000_write(E1000_RDBAL, rx_ring_phys);
  e1000_write(E1000_RDLEN, RX_DESC_COUNT * sizeof(rx_desc));
  e1000_write(E1000_RDH, 0);
  e1000_write(E1000_RDT, RX_DESC_COUNT - 1);

  // Enable receiver with proper flags
  e1000_write(E1000_RCTL,
              (1 << 1) |      // EN - Enable
                  (1 << 2) |  // SBP - Store Bad Packets
                  (1 << 4) |  // MPE - Multicast Promiscuous Enable
                  (1 << 15) | // BAM - Broadcast Accept Mode
                  (0 << 16) | // BSIZE = 2048 (00)
                  (1 << 26)); // SECRC - Strip Ethernet CRC

  // Setup TX ring
  for (int i = 0; i < TX_DESC_COUNT; i++) {
    tx_ring[i].addr = virt_to_phys(tx_buffers[i]);
    tx_ring[i].status = 0x1;
  }

  e1000_write(E1000_TDBAL, virt_to_phys((void *)tx_ring));
  e1000_write(E1000_TDLEN, TX_DESC_COUNT * sizeof(tx_desc));
  e1000_write(E1000_TDH, 0);
  e1000_write(E1000_TDT, 0);

  e1000_write(E1000_TCTL,
              (1 << 1) |         // Enable transmitter
                  (1 << 3) |     // Pad short packets
                  (0x10 << 4) |  // Collision threshold
                  (0x40 << 12)); // Collision distance

  e1000_write(E1000_TIPG, 0x0060200A);

  // VERIFICATION: Dump all critical RX registers
  serial_log("e1000: === RX Register Verification ===");
  serial_log_hex("  RCTL  = ", e1000_read(E1000_RCTL));
  serial_log_hex("  RDBAL = ", e1000_read(E1000_RDBAL));
  serial_log_hex("  RDLEN = ", e1000_read(E1000_RDLEN));
  serial_log_hex("  RDH   = ", e1000_read(E1000_RDH));
  serial_log_hex("  RDT   = ", e1000_read(E1000_RDT));
  serial_log("e1000: Ready.");
}

// =======================================================
// SEND PACKET
// =======================================================

extern "C" void e1000_send(void *data, uint16_t length) {
  extern void serial_log(const char *);
  extern void serial_log_hex(const char *, uint32_t);

  tx_desc *desc = &tx_ring[tx_tail];
  uint8_t *buf = tx_buffers[tx_tail];

  for (int i = 0; i < length; i++)
    buf[i] = ((uint8_t *)data)[i];

  desc->length = length;
  desc->cmd = (1 << 0) | (1 << 3); // EOP | RS
  desc->status = 0;

  serial_log_hex("e1000: TX packet len: ", length);

  tx_tail = (tx_tail + 1) % TX_DESC_COUNT;
  e1000_write(E1000_TDT, tx_tail);

  // Wait for transmit to complete
  for (volatile int i = 0; i < 100000; i++) {
    if (desc->status & 0x1) {
      serial_log("e1000: TX complete.");
      break;
    }
  }
}

// =======================================================
// RECEIVE PACKET (Polling)
// =======================================================

extern "C" int e1000_receive(uint8_t *out) {
  extern void serial_log(const char *);
  extern void serial_log_hex(const char *, uint32_t);

  // Debug: Log first 10 calls to verify we're being called
  static int first_calls = 0;
  if (first_calls < 10) {
    first_calls++;
    serial_log("e1000: RX poll called");
  }

  rx_desc *desc = &rx_ring[rx_tail];
  uint8_t status = desc->status;

  // Periodic debug regardless of packet
  static int poll_count = 0;
  if (++poll_count >= 1000) {
    poll_count = 0;
    uint32_t rdh = e1000_read(E1000_RDH);
    uint32_t rdt = e1000_read(E1000_RDT);
    serial_log_hex("e1000: RDH=", rdh);
    serial_log_hex("       RDT=", rdt);
    serial_log_hex("       rx_tail=", rx_tail);
    serial_log_hex("       status=", status);
  }

  // Check if descriptor has packet (DD bit = bit 0)
  if (!(status & 0x01)) {
    return 0;
  }

  uint16_t len = desc->length;
  serial_log_hex("e1000: RX packet! len=", len);

  for (int i = 0; i < len; i++)
    out[i] = rx_buffers[rx_tail][i];

  desc->status = 0;
  uint32_t last_tail = rx_tail;
  rx_tail = (rx_tail + 1) % RX_DESC_COUNT;
  e1000_write(E1000_RDT, last_tail);

  return len;
}
