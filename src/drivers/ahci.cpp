#include "ahci.h"
#include "pci.h"
#include "serial.h"
extern "C" void kernel_yield();
#include "../kernel/heap.h"
#include "../kernel/paging.h"
#include "../kernel/memory.h"
#include "../kernel/pmm.h"
#include "../include/string.h"

extern "C" {

// ============================================================================
// GLOBAL STATE
// ============================================================================

static hba_mem_t  *g_abar        = nullptr;
ahci_device_t      g_ahci_devices[MAX_AHCI_DEVICES];
uint32_t           g_ahci_device_count = 0;

static void ahci_acquire(uint32_t drive_id) {
    if (drive_id >= g_ahci_device_count) return;
    
    // Rev AF: If we are on Core 2 (Dedicated AI), don't yield. Just spin fast.
    // This removes the 10ms scheduler jitter from the 60MB model load.
    int cpu_id = 0;
    asm volatile("mov $1, %%eax; cpuid; shrl $24, %%ebx" : "=b"(cpu_id) : : "eax", "ecx", "edx");
    bool is_ai_core = (cpu_id == 2);

    int spin_count = 0;
    while (__sync_lock_test_and_set(&g_ahci_devices[drive_id].lock, 1)) {
        asm volatile("pause");
        if (!is_ai_core) {
            spin_count++;
            if (spin_count > 100) {
                kernel_yield(); 
                spin_count = 0;
            }
        }
    }
}

static void ahci_release(uint32_t drive_id) {
    if (drive_id >= g_ahci_device_count) return;
    __sync_lock_release(&g_ahci_devices[drive_id].lock);
}

// ============================================================================
// PORT CONTROL
// ============================================================================

static void ahci_stop_cmd(hba_port_t *port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    int timeout = 1000000;
    while (timeout-- > 0) {
        uint32_t cmd = port->cmd;
        if (!(cmd & HBA_PxCMD_FR) && !(cmd & HBA_PxCMD_CR))
            return;
    }
    serial_log("AHCI: WARNING — port stop timeout");
}

static void ahci_start_cmd(hba_port_t *port) {
    int timeout = 1000000;
    while ((port->cmd & HBA_PxCMD_CR) && timeout-- > 0);

    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

static int ahci_find_cmd_slot(hba_port_t *port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & (1u << i)) == 0) return i;
    }
    return -1;
}

// ============================================================================
// COMMON COMMAND ISSUE + POLL (used by read, write, and identify)
// ============================================================================

static bool ahci_issue_and_poll(hba_port_t *port) {
    // Wait for port idle (BSY=0, DRQ=0)
    volatile uint32_t *tfd_reg = &port->tfd;
    int spin = 0;
    while ((*tfd_reg & (0x80 | 0x08)) && spin < 1000000) {
        spin++;
        asm volatile("pause");
    }
    if (spin >= 1000000) {
        serial_log("AHCI: Port busy timeout before command");
        return false;
    }

    // Issue command on slot 0
    port->ci = 1;
    asm volatile("mfence" ::: "memory");

    // Poll for completion
    volatile uint32_t *ci_reg = &port->ci;
    volatile uint32_t *is_reg = &port->is;

    for (int i = 0; i < 10000000; i++) {
        uint32_t ci_val = *ci_reg;
        uint32_t is_val = *is_reg;

        // Task file error
        if (is_val & (1u << 30)) {
            serial_log("AHCI: Task file error");
            serial_log_hex("AHCI: SERR = ", port->serr);
            serial_log_hex("AHCI: TFD  = ", port->tfd);
            port->serr = 0xFFFFFFFF;
            port->is = 0xFFFFFFFF;
            return false;
        }

        // Command completed
        if (ci_val == 0) {
            port->is = 0xFFFFFFFF;
            return true;
        }

        if (i % 1000 == 0) {
            asm volatile("pause");
            kernel_yield();
        }
    }

    serial_log("AHCI: Command completion timeout");
    serial_log_hex("AHCI: CI   = ", *ci_reg);
    serial_log_hex("AHCI: TFD  = ", *tfd_reg);
    serial_log_hex("AHCI: IS   = ", *is_reg);
    serial_log_hex("AHCI: SERR = ", port->serr);
    return false;
}

// ============================================================================
// SETUP COMMAND HEADER (common for all commands)
// ============================================================================

static void ahci_setup_cmd_header(ahci_device_t *dev, bool is_write, uint16_t prdt_count) {
    hba_cmd_header_t *cmd = &dev->clb_virt[0];
    cmd->cfl   = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmd->w     = is_write ? 1 : 0;
    cmd->a     = 0;
    cmd->p     = 0;
    cmd->r     = 0;
    cmd->b     = 0;
    cmd->c     = 1;
    cmd->pmp   = 0;
    cmd->prdtl = prdt_count;
    cmd->prdbc = 0;
    cmd->ctba  = dev->ctbl_phys;
    cmd->ctbau = 0;
}

// ============================================================================
// IDENTIFY DEVICE
// ============================================================================

static bool ahci_identify(ahci_device_t *dev) {
    if (!dev->ready || !dev->port) return false;

    hba_port_t *port = dev->port;

    port->is   = (uint32_t)-1;
    port->serr = (uint32_t)-1;

    // Setup command header
    ahci_setup_cmd_header(dev, false, 1);

    // Setup command table
    hba_cmd_table_t *tbl = dev->ctbl_virt;
    memset(tbl, 0, sizeof(hba_cmd_table_t));

    // PRDT: 512 bytes into bounce buffer
    tbl->prdt_entry[0].dba  = dev->bounce_phys;
    tbl->prdt_entry[0].dbau = 0;
    tbl->prdt_entry[0].dbc  = 511;
    tbl->prdt_entry[0].i    = 1;

    // Build FIS for IDENTIFY DEVICE (0xEC)
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    memset(fis, 0, sizeof(fis_reg_h2d_t));
    fis->fis_type = 0x27;
    fis->c        = 1;
    fis->command  = 0xEC;
    fis->device   = 0;

    // Clear bounce buffer before IDENTIFY
    memset(dev->bounce_virt, 0, 512);

    if (!ahci_issue_and_poll(port)) {
        serial_log("AHCI: IDENTIFY DEVICE failed");
        return false;
    }

    // Parse IDENTIFY data (256 x uint16_t words)
    uint16_t *id = (uint16_t *)dev->bounce_virt;

    // Words 100-103: LBA48 total sectors (definitive for modern drives)
    uint64_t lba48 = (uint64_t)id[100]
                   | ((uint64_t)id[101] << 16)
                   | ((uint64_t)id[102] << 32)
                   | ((uint64_t)id[103] << 48);

    if (lba48 > 0) {
        dev->total_sectors = lba48;
    } else {
        // Fallback: Words 60-61: LBA28 total sectors
        uint32_t lba28 = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
        dev->total_sectors = lba28;
    }

    serial_log("AHCI: IDENTIFY successful.");
    return true;
}


// ============================================================================
// INIT
// ============================================================================

void ahci_init(void) {
    uint8_t bus, slot, func;

    serial_log("AHCI: Searching for SATA/AHCI Controller...");
    if (!pci_find_by_class(0x01, 0x06, 0x01, &bus, &slot, &func)) {
        if (!pci_find_by_class(0x01, 0x01, 0x8F, &bus, &slot, &func)) {
            serial_log("AHCI: No compatible AHCI controller found.");
            return;
        }
    }

    uint32_t pci_cmd = pci_read_config(bus, slot, func, 0x04);
    pci_cmd |= (1 << 1) | (1 << 2);
    pci_write_config(bus, slot, func, 0x04, pci_cmd);

    uint32_t bar5 = pci_read_config(bus, slot, func, 0x24);
    uint32_t phys_addr = bar5 & 0xFFFFF000;

    for (int i = 0; i < AHCI_MMIO_PAGES; i++) {
        paging_map(phys_addr + (i * 0x1000), AHCI_MMIO_VIRT + (i * 0x1000), 0x1B);
    }

    g_abar = (hba_mem_t *)AHCI_MMIO_VIRT;
    
    // Rev N: Robust HBA Reset
    serial_log("AHCI: Performing HBA Reset...");
    g_abar->ghc |= 1; // HR (HBA Reset)
    int reset_timeout = 1000000;
    while ((g_abar->ghc & 1) && reset_timeout-- > 0) asm volatile("pause");
    
    g_abar->ghc |= (1u << 31); // AE (AHCI Enable)
    
    // Rev O: Stabilization delay after AHCI Enable
    for(volatile int i = 0; i < 1000000; i++) asm volatile("pause");

    serial_log_hex("AHCI: CAP  = ", g_abar->cap);
    serial_log_hex("AHCI: PI   = ", g_abar->pi);
    serial_log_hex("AHCI: VS   = ", g_abar->vs);

    uint32_t pi = g_abar->pi;
    for (int i = 0; i < 32; i++) {
        if (!(pi & (1u << i))) continue;
        hba_port_t *port = &g_abar->ports[i];
        serial_log_hex("AHCI: Probing Port ", i);
        serial_log_hex("  SSTS: ", port->ssts);

        if ((port->ssts & 0x0F) != HBA_PORT_DET_PRESENT)
            continue;

        if (g_ahci_device_count >= MAX_AHCI_DEVICES) break;

        ahci_device_t *dev = &g_ahci_devices[g_ahci_device_count];
        dev->port = port;
        dev->lock = 0;

        // Rev P: IMPORTANT - Must stop port and setup FIS buffers BEFORE probing SIG
        // Otherwise, the HBA has nowhere to store the signature information.
        ahci_stop_cmd(port);

        uint32_t cl_phys = (uint32_t)(uintptr_t)pmm_alloc_block();
        dev->clb_virt = (hba_cmd_header_t *)PHYS_TO_VIRT(cl_phys);
        dev->clb_phys = cl_phys;
        port->clb = cl_phys;
        port->clbu = 0;

        uint32_t fb_phys = (uint32_t)(uintptr_t)pmm_alloc_block();
        dev->fb_virt = (uint8_t *)PHYS_TO_VIRT(fb_phys);
        dev->fb_phys = fb_phys;
        port->fb = fb_phys;
        port->fbu = 0;

        // Enable FIS reception to allow the signature FIS to be received
        port->cmd |= HBA_PxCMD_FRE;

        // Rev P: Wait for signature to arrive (now that FRE is enabled)
        serial_log("  FIS mailbox ready. Waiting for SIG...");
        int sig_timeout = 200; // Shorter timeout for better boot speed
        while (port->sig == 0xFFFFFFFF && sig_timeout-- > 0) {
            for(volatile int delay = 0; delay < 100000; delay++) asm volatile("pause");
        }
        serial_log_hex("  Final SIG: ", port->sig);

        if (port->sig != SATA_SIG_ATA) {
            serial_log("  Not a SATA drive, skipping.");
            continue;
        }

        uint32_t ct_phys = (uint32_t)(uintptr_t)pmm_alloc_block();
        dev->ctbl_virt = (hba_cmd_table_t *)PHYS_TO_VIRT(ct_phys);
        dev->ctbl_phys = ct_phys;
        dev->clb_virt[0].ctba = ct_phys;

        uint32_t dma_phys = (uint32_t)(uintptr_t)pmm_alloc_contiguous_blocks(256);
        if (dma_phys) {
            dev->bounce_sectors = 256;
        } else {
            dma_phys = (uint32_t)(uintptr_t)pmm_alloc_block();
            dev->bounce_sectors = 8;
        }
        dev->bounce_virt = (uint8_t *)PHYS_TO_VIRT(dma_phys);
        dev->bounce_phys = dma_phys;

        port->serr = 0xFFFFFFFF;
        port->is = 0xFFFFFFFF;
        
        ahci_start_cmd(port);

        dev->ready = true;
        ahci_identify(dev);

        serial_log_hex("AHCI: Port Initialized: ", i);
        g_ahci_device_count++;
    }
}

bool ahci_is_ready(uint32_t drive_id) {
    if (drive_id >= g_ahci_device_count) return false;
    return g_ahci_devices[drive_id].ready;
}

hba_port_t *ahci_get_port(uint32_t drive_id) {
    if (drive_id >= g_ahci_device_count) return nullptr;
    return g_ahci_devices[drive_id].port;
}

uint64_t ahci_get_total_sectors(uint32_t drive_id) {
    if (drive_id >= g_ahci_device_count) return 0;
    return g_ahci_devices[drive_id].total_sectors;
}

// ============================================================================
// LOW-LEVEL DMA READ
// ============================================================================

static bool ahci_do_read(ahci_device_t *dev, uint64_t lba, uint32_t count) {
    if (!dev->ready || !dev->port || count == 0 || count > dev->bounce_sectors)
        return false;

    hba_port_t *port = dev->port;

    port->is   = (uint32_t)-1;
    port->serr = (uint32_t)-1;

    ahci_setup_cmd_header(dev, false, 1);

    hba_cmd_table_t *tbl = dev->ctbl_virt;
    memset(tbl, 0, sizeof(hba_cmd_table_t));

    tbl->prdt_entry[0].dba  = dev->bounce_phys;
    tbl->prdt_entry[0].dbau = 0;
    tbl->prdt_entry[0].dbc  = (count * 512) - 1;
    tbl->prdt_entry[0].i    = 1;

    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    memset(fis, 0, sizeof(fis_reg_h2d_t));
    fis->fis_type = 0x27;
    fis->c        = 1;
    fis->command  = 0x25;  // READ DMA EXT
    fis->device   = 1 << 6;

    fis->lba0 = (uint8_t)(lba);
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->countl = (uint8_t)(count);
    fis->counth = (uint8_t)(count >> 8);

    return ahci_issue_and_poll(port);
}

// ============================================================================
// LOW-LEVEL DMA WRITE
// ============================================================================

static bool ahci_do_write(ahci_device_t *dev, uint64_t lba, uint32_t count) {
    if (!dev->ready || !dev->port || count == 0 || count > dev->bounce_sectors)
        return false;

    hba_port_t *port = dev->port;

    port->is   = (uint32_t)-1;
    port->serr = (uint32_t)-1;

    ahci_setup_cmd_header(dev, true, 1);

    hba_cmd_table_t *tbl = dev->ctbl_virt;
    memset(tbl, 0, sizeof(hba_cmd_table_t));

    tbl->prdt_entry[0].dba  = dev->bounce_phys;
    tbl->prdt_entry[0].dbau = 0;
    tbl->prdt_entry[0].dbc  = (count * 512) - 1;
    tbl->prdt_entry[0].i    = 1;

    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    memset(fis, 0, sizeof(fis_reg_h2d_t));
    fis->fis_type = 0x27;
    fis->c        = 1;
    fis->command  = 0x35;  // WRITE DMA EXT
    fis->device   = 1 << 6;

    fis->lba0 = (uint8_t)(lba);
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->countl = (uint8_t)(count);
    fis->counth = (uint8_t)(count >> 8);

    return ahci_issue_and_poll(port);
}

// ============================================================================
// PUBLIC API: READ SECTORS
// ============================================================================

bool ahci_read_sectors(uint32_t drive_id, uint64_t lba, uint32_t count, void *buffer) {
    if (drive_id >= g_ahci_device_count || !buffer || count == 0) return false;
    ahci_device_t *dev = &g_ahci_devices[drive_id];
    if (!dev->ready) return false;

    uint8_t *dest = (uint8_t *)buffer;
    uint32_t remaining = count;
    uintptr_t cur_lba = (uintptr_t)lba;

    while (remaining > 0) {
        ahci_acquire(drive_id);
        
        uint32_t chunk = (remaining > dev->bounce_sectors) ? dev->bounce_sectors : remaining;

        if (!ahci_do_read(dev, cur_lba, chunk)) {
            ahci_release(drive_id);
            return false;
        }
        memcpy(dest, dev->bounce_virt, chunk * 512);

        ahci_release(drive_id);
        
        dest      += chunk * 512;
        cur_lba   += chunk;
        remaining -= chunk;
        
        // Parallelism help: tiny pause between chunks
        asm volatile("pause");
    }

    return true;
}

// ============================================================================
// PUBLIC API: WRITE SECTORS
// ============================================================================

bool ahci_write_sectors(uint32_t drive_id, uint64_t lba, uint32_t count, const void *buffer) {
    if (drive_id >= g_ahci_device_count || !buffer || count == 0) return false;
    ahci_device_t *dev = &g_ahci_devices[drive_id];
    if (!dev->ready) return false;

    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t remaining = count;
    uintptr_t cur_lba = (uintptr_t)lba;

    while (remaining > 0) {
        ahci_acquire(drive_id);
        
        uint32_t chunk = (remaining > dev->bounce_sectors) ? dev->bounce_sectors : remaining;

        memcpy(dev->bounce_virt, src, chunk * 512);

        if (!ahci_do_write(dev, cur_lba, chunk)) {
            ahci_release(drive_id);
            return false;
        }
        
        ahci_release(drive_id);

        src       += chunk * 512;
        cur_lba   += chunk;
        remaining -= chunk;
        
        asm volatile("pause");
    }

    return true;
}

} // extern "C"
