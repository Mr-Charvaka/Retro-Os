// kernel_e1000.cpp
// Intel e1000 Network Driver - DEDICATED DMA POOL VERSION
// 32-bit x86, MMIO, DMA-capable
// Retro-OS Networking Phase 1

#include "../drivers/pci.h"
#include "paging.h"
#include "../drivers/serial.h"
#include <stddef.h>
#include <stdint.h>

// =======================================================
// DEDICATED NETWORK DMA ARCHITECTURE
// =======================================================
// We use a fixed physical region (18MB - 20MB) for all Network DMA
// This ensures isolation from the kernel and prevents QEMU Translator crashes.
#define NET_DMA_PHYS_BASE 0x01200000 
#define NET_DMA_VIRT_BASE (NET_DMA_PHYS_BASE + 0xC0000000)

// Offsets within the 2MB pool
#define RX_RING_OFFSET    0x00000
#define TX_RING_OFFSET    0x01000
#define RX_BUF_OFFSET     0x02000
#define TX_BUF_OFFSET     0x12000

// =======================================================
// BRIDGE TO RETRO-OS
#include "net.h"
extern "C" uint8_t my_mac[6];

extern "C" uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return pci_read_config(bus, slot, func, offset);
}

extern "C" void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    pci_write_config(bus, slot, func, offset, value);
}

extern "C" uint32_t virt_to_phys(void *addr) {
    uintptr_t a = (uintptr_t)addr;
    if (a >= 0xC0000000) return (uint32_t)(a - 0xC0000000);
    return 0; // Hardware DMA should never touch user space here
}

extern "C" void *map_mmio(uint32_t phys_addr, size_t size) {
    uint32_t pages = (size + 4095) / 4096;
    for (uint32_t i = 0; i < pages; i++) {
        paging_map(phys_addr + (i * 4096), phys_addr + (i * 4096), 3);
    }
    return (void *)phys_addr;
}

// =======================================================
// e1000 REGISTERS & DESCRIPTORS
#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_TCTL      0x0400
#define E1000_TIPG      0x0410
#define E1000_TDBAL     0x3800
#define E1000_TDBAH     0x3804
#define E1000_TDLEN     0x3808
#define E1000_TDH       0x3810
#define E1000_TDT       0x3818
#define E1000_RCTL      0x0100
#define E1000_RDBAL     0x2800
#define E1000_RDBAH     0x2804
#define E1000_RDLEN     0x2808
#define E1000_RDH       0x2810
#define E1000_RDT       0x2818

#define RX_DESC_COUNT 32
#define TX_DESC_COUNT 32

struct rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    volatile uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    volatile uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

// =======================================================
// DRIVER STATE (Using Fixed Pool)
static volatile uint32_t *e1000_mmio = nullptr;
static rx_desc *rx_ring = (rx_desc*)(NET_DMA_VIRT_BASE + RX_RING_OFFSET);
static tx_desc *tx_ring = (tx_desc*)(NET_DMA_VIRT_BASE + TX_RING_OFFSET);
static uint8_t (*rx_buffers)[2048] = (uint8_t(*)[2048])(NET_DMA_VIRT_BASE + RX_BUF_OFFSET);
static uint8_t (*tx_buffers)[2048] = (uint8_t(*)[2048])(NET_DMA_VIRT_BASE + TX_BUF_OFFSET);

static uint32_t rx_tail = 0;
static uint32_t tx_tail = 0;

static inline void e1000_write(uint32_t reg, uint32_t val) {
    if (e1000_mmio) e1000_mmio[reg / 4] = val;
}

static inline uint32_t e1000_read(uint32_t reg) {
    return e1000_mmio ? e1000_mmio[reg / 4] : 0;
}

// =======================================================
// INITIALIZATION
extern "C" void e1000_init(uint8_t bus, uint8_t slot, uint8_t func) {
    serial_log("e1000: Initializing Intel Network Card...");

    // 1. Map MMIO
    uint32_t bar0 = pci_read(bus, slot, func, 0x10);
    uint32_t mmio_phys = bar0 & ~0xF;
    e1000_mmio = (uint32_t *)map_mmio(mmio_phys, 0x20000);
    
    serial_log_hex("e1000: MMIO Base: ", mmio_phys);
    serial_log_hex("e1000: DMA Pool Phys: ", NET_DMA_PHYS_BASE);

    // 2. Enable PCI Bus Mastering
    uint32_t pci_cmd = pci_read(bus, slot, func, 0x04);
    pci_cmd |= (1 << 2) | (1 << 1); 
    pci_write(bus, slot, func, 0x04, pci_cmd);

    // 3. Reset Device
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | (1 << 26));
    for (volatile int i = 0; i < 100000; i++); 

    // Set Link Up
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | (1 << 6));

    // 4. Setup MAC address
    uint32_t ral = e1000_read(0x5400);
    uint32_t rah = e1000_read(0x5404);
    my_mac[0] = ral & 0xFF;
    my_mac[1] = (ral >> 8) & 0xFF;
    my_mac[2] = (ral >> 16) & 0xFF;
    my_mac[3] = (ral >> 24) & 0xFF;
    my_mac[4] = rah & 0xFF;
    my_mac[5] = (rah >> 8) & 0xFF;

    // 5. Clear multicast table
    for (int i = 0; i < 128; i++) e1000_write(0x5200 + (i * 4), 0);

    // 6. Setup RX Ring
    for (int i = 0; i < RX_DESC_COUNT; i++) {
        rx_ring[i].addr = NET_DMA_PHYS_BASE + RX_BUF_OFFSET + (i * 2048);
        rx_ring[i].status = 0;
    }
    e1000_write(E1000_RDBAL, NET_DMA_PHYS_BASE + RX_RING_OFFSET);
    e1000_write(E1000_RDBAH, 0);
    e1000_write(E1000_RDLEN, RX_DESC_COUNT * sizeof(rx_desc));
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, RX_DESC_COUNT - 1);
    e1000_write(E1000_RCTL, (1 << 1) | (1 << 2) | (1 << 4) | (1 << 15) | (1 << 26));

    // 7. Setup TX Ring
    for (int i = 0; i < TX_DESC_COUNT; i++) {
        tx_ring[i].addr = NET_DMA_PHYS_BASE + TX_BUF_OFFSET + (i * 2048);
        tx_ring[i].status = 0x1; // DD bit set initially
    }
    e1000_write(E1000_TDBAL, NET_DMA_PHYS_BASE + TX_RING_OFFSET);
    e1000_write(E1000_TDBAH, 0);
    e1000_write(E1000_TDLEN, TX_DESC_COUNT * sizeof(tx_desc));
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    e1000_write(E1000_TCTL, (1 << 1) | (1 << 3) | (0x10 << 4) | (0x40 << 12));
    e1000_write(E1000_TIPG, 0x0060200A);

    serial_log("e1000: Initialization complete using Isolated DMA Pool.");
}

static volatile int tx_lock = 0;

extern "C" void e1000_send(void *data, uint16_t length) {
    if (!e1000_mmio) return;
    
    // Safety Spinlock
    while (__sync_lock_test_and_set(&tx_lock, 1)) {
        for (volatile int i = 0; i < 1000; i++); 
    }

    tx_desc *desc = &tx_ring[tx_tail];
    uint8_t *buf = tx_buffers[tx_tail];

    // Safety: ensure packet isn't too large
    if (length > 2000) length = 2000;

    // Copy data to DMA buffer
    for (int i = 0; i < length; i++) buf[i] = ((uint8_t *)data)[i];

    desc->length = length;
    desc->cmd = (1 << 0) | (1 << 3); // EOP | RS
    desc->status = 0;

    tx_tail = (tx_tail + 1) % TX_DESC_COUNT;
    e1000_write(E1000_TDT, tx_tail);

    // Wait for transmit to complete (DD bit in status)
    for (uint32_t i = 0; i < 1000000; i++) {
        if (desc->status & 0x1) break;
    }

    __sync_lock_release(&tx_lock);
}

extern "C" int e1000_receive(uint8_t *out) {
    rx_desc *desc = &rx_ring[rx_tail];
    if (!(desc->status & 0x01)) return 0; // Not ready

    uint16_t len = desc->length;
    for (int i = 0; i < len; i++) out[i] = rx_buffers[rx_tail][i];

    desc->status = 0; // Clear DD bit
    uint32_t old_rx_tail = rx_tail;
    rx_tail = (rx_tail + 1) % RX_DESC_COUNT;
    e1000_write(E1000_RDT, old_rx_tail);

    return len;
}
