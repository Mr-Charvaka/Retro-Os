#include "ac97.h"
#include "../include/io.h"
#include "pci.h"
#include "serial.h"
#include "../kernel/paging.h"
#include "../kernel/heap.h"

// Intel AC97 Native Audio Bus Master (NAM)
#define AC97_NAM_BAR   0x14
#define AC97_NAM_POB   0x10  // PCM Out Buffer Descriptor List Base
#define AC97_NAM_PIV   0x14  // PCM Out Index Value (Last processed)
#define AC97_NAM_LVI   0x15  // PCM Out Last Valid Index
#define AC97_NAM_SR    0x16  // PCM Out Status Register
#define AC97_NAM_CR    0x1B  // PCM Out Control Register

// Native Audio Mixer (NAM) - Mixer ports
#define AC97_MIXER_BAR 0x10
#define AC97_MIXER_MV  0x02  // Master Volume
#define AC97_MIXER_PCM 0x18  // PCM Volume

struct AC97_BDL {
    uint32_t addr;
    uint16_t length;
    uint16_t reserved : 14;
    uint16_t bup : 1;
    uint16_t ioc : 1;
} __attribute__((packed));

static uint32_t nam_base = 0;
static uint32_t mixer_base = 0;
static AC97_BDL *bdl_list = nullptr;

void ac97_init() {
    uint8_t bus, slot, func;
    // Common AC97 PCI Vendor/Device (QEMU default)
    if (pci_find_device(0x8086, 0x2415, &bus, &slot, &func)) {
        serial_log("FLAGSHIP AUDIO: AC97 Controller Detected (8086:2415)");
        
        // 1. Enable Bus Mastering and IO Space
        uint16_t command = pci_read_config(bus, slot, func, 0x04) & 0xFFFF;
        command |= (1 << 2) | (1 << 0); // BM + IO
        pci_write_config(bus, slot, func, 0x04, command);

        // 2. Get BARs
        mixer_base = pci_read_config(bus, slot, func, 0x10) & ~0x1;
        nam_base   = pci_read_config(bus, slot, func, 0x14) & ~0x1;

        // 3. Reset Mixer and Unmute
        outw(mixer_base + AC97_MIXER_MV,  0x0000); // 0 = Max volume, 0x8000 = Mute
        outw(mixer_base + AC97_MIXER_PCM, 0x0000);

        // 4. Allocate BDL (32 entries max supported by AC97)
        bdl_list = (AC97_BDL*)malloc(sizeof(AC97_BDL) * 32);
        
        serial_log("FLAGSHIP AUDIO: Hardware Ready.");
    } else {
        serial_log("FLAGSHIP AUDIO: AC97 Controller not found.");
    }
}

void ac97_play(uint32_t phys_addr, uint32_t count) {
    if (!nam_base || !bdl_list) return;

    // Stop current play
    outb(nam_base + AC97_NAM_CR, 0);

    // Setup BDL for a single large buffer (simplified for now)
    // Range 0..31
    for (int i = 0; i < 32; i++) {
        bdl_list[i].addr = phys_addr;
        bdl_list[i].length = (uint16_t)(count / 2); // Sample units? Actually registers are bytes/2
        bdl_list[i].ioc = 1;
        bdl_list[i].bup = 0;
        bdl_list[i].reserved = 0;
    }

    // Set BDL Base
    outl(nam_base + AC97_NAM_POB, (uint32_t)(uintptr_t)VIRT_TO_PHYS(bdl_list));

    // Reset current index
    outb(nam_base + AC97_NAM_LVI, 31); // Use all slots potentially
    
    // Start PCM Out
    outb(nam_base + AC97_NAM_CR, 1); // FE (Run)
}

bool ac97_is_playing() {
    if (!nam_base) return false;
    return (inb(nam_base + AC97_NAM_SR) & 1); // RUN status
}
