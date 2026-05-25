#include "nvme.h"
#include "pci.h"
#include "serial.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../kernel/pmm.h"
#include "../kernel/paging.h"
#include "../kernel/pae.h"
#include "../kernel/pae.h"
#include "../kernel/heap.h"
#include "../kernel/apic.h"

extern "C" void kernel_yield();

extern "C" {

// =============================================================================
// GLOBAL STATE
// =============================================================================

static nvme_device_t g_nvme;
static nvme_pending_t g_pending[NVME_MAX_PENDING];

// Translation helper for PRPs
#include "../kernel/paging.h"
extern "C" bool g_pae_active;

static uint64_t nvme_virt_to_phys(uint32_t virt) {
    return (uint64_t)paging_get_phys_generic(virt);
}

// =============================================================================
// MMIO HELPERS
// =============================================================================

static inline uint32_t nvme_read32(uint32_t offset) {
    return *(volatile uint32_t *)(g_nvme.mmio + offset);
}

static inline void nvme_write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(g_nvme.mmio + offset) = value;
}

static inline uint64_t nvme_read64(uint32_t offset) {
    uint32_t lo = nvme_read32(offset);
    uint32_t hi = nvme_read32(offset + 4);
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline void nvme_write64(uint32_t offset, uint64_t value) {
    nvme_write32(offset, (uint32_t)(value & 0xFFFFFFFF));
    nvme_write32(offset + 4, (uint32_t)(value >> 32));
}

// =============================================================================
// DOORBELL HELPERS
//
// Admin SQ doorbell  = 0x1000 + (2 * 0) * stride = 0x1000
// Admin CQ doorbell  = 0x1000 + (2 * 0 + 1) * stride
// I/O SQ 1 doorbell  = 0x1000 + (2 * 1) * stride
// I/O CQ 1 doorbell  = 0x1000 + (2 * 1 + 1) * stride
// =============================================================================

static inline void nvme_ring_admin_sq_doorbell(void) {
    uint32_t offset = 0x1000 + (0) * g_nvme.doorbell_stride;
    nvme_write32(offset, g_nvme.admin_sq_tail);
}

static inline void nvme_ring_admin_cq_doorbell(void) {
    uint32_t offset = 0x1000 + (1) * g_nvme.doorbell_stride;
    nvme_write32(offset, g_nvme.admin_cq_head);
}

static inline void nvme_ring_io_sq_doorbell(void) {
    uint32_t offset = 0x1000 + (2) * g_nvme.doorbell_stride;
    nvme_write32(offset, g_nvme.io_sq_tail);
}

static inline void nvme_ring_io_cq_doorbell(void) {
    uint32_t offset = 0x1000 + (3) * g_nvme.doorbell_stride;
    nvme_write32(offset, g_nvme.io_cq_head);
}

// =============================================================================
// COMMAND ID GENERATION
// =============================================================================

static uint16_t nvme_alloc_cid(void) {
    return g_nvme.next_cid++;
}

// =============================================================================
// ADMIN COMMAND SUBMISSION + POLLING
// =============================================================================

static void nvme_submit_admin_cmd(nvme_sq_entry_t *cmd) {
    memcpy(&g_nvme.admin_sq[g_nvme.admin_sq_tail], cmd, sizeof(nvme_sq_entry_t));
    g_nvme.admin_sq_tail = (g_nvme.admin_sq_tail + 1) % NVME_ADMIN_QUEUE_DEPTH;
    nvme_ring_admin_sq_doorbell();
}

static int nvme_wait_admin_completion(uint16_t expected_cid) {
    // Timeout: CAP.TO * 500ms, approximate with a spin counter
    // At ~1GHz emulated, 500M iterations ≈ 500ms
    uint32_t timeout = 50000000;

    while (timeout--) {
        nvme_cq_entry_t *cqe = &g_nvme.admin_cq[g_nvme.admin_cq_head];
        uint8_t phase = cqe->status & 1;

        if (phase == g_nvme.admin_cq_phase) {
            // New completion available
            uint16_t status_code = (cqe->status >> 1) & 0x7FF;
            uint16_t completed_cid = cqe->cid;

            // Advance CQ head
            g_nvme.admin_cq_head = (g_nvme.admin_cq_head + 1) % NVME_ADMIN_QUEUE_DEPTH;
            if (g_nvme.admin_cq_head == 0) {
                g_nvme.admin_cq_phase ^= 1;
            }

            // Ring CQ doorbell to inform controller we consumed it
            nvme_ring_admin_cq_doorbell();

            if (completed_cid != expected_cid) {
                serial_log("NVMe: WARNING admin CID mismatch!");
                serial_log_hex("  Expected: ", expected_cid);
                serial_log_hex("  Got:      ", completed_cid);
            }

            if (status_code != 0) {
                serial_log_hex("NVMe: Admin command FAILED, status=", status_code);
                g_nvme.total_errors++;
                return -1;
            }
            return 0;
        }
    }

    serial_log("NVMe: Admin command TIMEOUT!");
    g_nvme.total_errors++;
    return -1;
}

// =============================================================================
// PCI DISCOVERY
// =============================================================================

static bool nvme_pci_probe(void) {
    uint8_t bus, slot, func;

    if (!pci_find_by_class(NVME_PCI_CLASS, NVME_PCI_SUBCLASS, NVME_PCI_PROG_IF,
                           &bus, &slot, &func)) {
        serial_log("NVMe: No controller found on PCI bus.");
        return false;
    }

    g_nvme.pci_bus  = bus;
    g_nvme.pci_slot = slot;
    g_nvme.pci_func = func;

    uint32_t id = pci_read_config(bus, slot, func, 0x00);
    serial_log("NVMe: Controller found!");
    serial_log_hex("  Bus:    ", bus);
    serial_log_hex("  Slot:   ", slot);
    serial_log_hex("  Vendor: ", id & 0xFFFF);
    serial_log_hex("  Device: ", (id >> 16) & 0xFFFF);

    // Read BAR0 (MMIO base address)
    uint32_t bar0_raw = pci_get_bar(bus, slot, func, 0);
    if (bar0_raw & 1) {
        serial_log("NVMe: ERROR — BAR0 is I/O space, not MMIO!");
        return false;
    }

    g_nvme.bar0_phys = bar0_raw & 0xFFFFFFF0;

    // Check if 64-bit BAR (bit 2:1 of BAR0)
    uint8_t bar_type = (bar0_raw >> 1) & 0x3;
    if (bar_type == 0x02) {
        // 64-bit BAR — upper 32 bits in BAR1
        uint32_t bar1 = pci_get_bar(bus, slot, func, 1);
        if (bar1 != 0) {
            serial_log("NVMe: WARNING — BAR0 is 64-bit with high bits set.");
            serial_log("NVMe: This 32-bit OS cannot address it. Trying anyway...");
            // In QEMU, upper bits are typically 0 for <4GB BARs
        }
    }

    serial_log_hex("NVMe: BAR0 Physical: ", g_nvme.bar0_phys);

    // Enable Bus Mastering + Memory Space access
    uint32_t pci_cmd = pci_read_config(bus, slot, func, 0x04);
    pci_cmd |= (1 << 1) | (1 << 2);  // Memory Space + Bus Master
    pci_write_config(bus, slot, func, 0x04, pci_cmd);

    serial_log("NVMe: PCI Bus Master + Memory Space enabled.");
    return true;
}

// =============================================================================
// MMIO MAPPING
// =============================================================================

static bool nvme_map_mmio(void) {
    // Map BAR0 into virtual address space
    // We need to map enough pages to cover the NVMe register set + doorbells
    // Typically 16KB-64KB depending on queue count
    uint32_t pages_needed = (NVME_MMIO_MAP_SIZE + 4095) / 4096;

    for (uint32_t i = 0; i < pages_needed; i++) {
        uint32_t phys = g_nvme.bar0_phys + (i * 4096);
        uint32_t virt = NVME_MMIO_VIRT_BASE + (i * 4096);
        paging_map(phys, virt, 3);  // Supervisor, RW, Present
    }

    g_nvme.mmio = (volatile uint8_t *)NVME_MMIO_VIRT_BASE;

    // Verify we can read the Version register
    uint32_t vs = nvme_read32(NVME_REG_VS);
    uint16_t major = (vs >> 16) & 0xFFFF;
    uint16_t minor = (vs >> 8) & 0xFF;
    serial_log_hex("NVMe: Version Major: ", major);
    serial_log_hex("NVMe: Version Minor: ", minor);

    if (major == 0 && minor == 0) {
        serial_log("NVMe: ERROR — Version register reads 0. MMIO mapping failed?");
        return false;
    }

    return true;
}

// =============================================================================
// CONTROLLER RESET + ENABLE
// =============================================================================

static bool nvme_disable_controller(void) {
    uint32_t cc = nvme_read32(NVME_REG_CC);
    cc &= ~NVME_CC_EN;
    nvme_write32(NVME_REG_CC, cc);

    // Wait for CSTS.RDY to become 0
    uint32_t timeout = 50000000;
    while (timeout--) {
        uint32_t csts = nvme_read32(NVME_REG_CSTS);
        if (!(csts & NVME_CSTS_RDY)) {
            serial_log("NVMe: Controller disabled.");
            return true;
        }
    }

    serial_log("NVMe: ERROR — Controller disable timeout!");
    return false;
}

static bool nvme_enable_controller(void) {
    uint32_t cc = NVME_CC_EN
                | NVME_CC_CSS_NVM
                | NVME_CC_MPS_4K
                | NVME_CC_AMS_RR
                | NVME_CC_IOSQES_64
                | NVME_CC_IOCQES_16;

    nvme_write32(NVME_REG_CC, cc);

    // Wait for CSTS.RDY
    uint32_t timeout = 50000000;
    while (timeout--) {
        uint32_t csts = nvme_read32(NVME_REG_CSTS);
        if (csts & NVME_CSTS_CFS) {
            serial_log("NVMe: FATAL — Controller Fatal Status during enable!");
            return false;
        }
        if (csts & NVME_CSTS_RDY) {
            serial_log("NVMe: Controller enabled and ready.");
            g_nvme.enabled = true;
            return true;
        }
    }

    serial_log("NVMe: ERROR — Controller enable timeout!");
    return false;
}

// =============================================================================
// ADMIN QUEUE SETUP
// =============================================================================

static bool nvme_setup_admin_queues(void) {
    // Allocate Admin Submission Queue (1 page = 4KB)
    // At 64 bytes per entry, 4KB holds 64 entries (we use 16)
    void *sq_phys = pmm_alloc_block();
    if (!sq_phys) {
        serial_log("NVMe: OOM allocating Admin SQ!");
        return false;
    }
    g_nvme.admin_sq_phys = (uint32_t)(uintptr_t)sq_phys;
    g_nvme.admin_sq = (nvme_sq_entry_t *)PHYS_TO_VIRT(g_nvme.admin_sq_phys);
    memset(g_nvme.admin_sq, 0, 4096);

    // Allocate Admin Completion Queue (1 page)
    void *cq_phys = pmm_alloc_block();
    if (!cq_phys) {
        serial_log("NVMe: OOM allocating Admin CQ!");
        return false;
    }
    g_nvme.admin_cq_phys = (uint32_t)(uintptr_t)cq_phys;
    g_nvme.admin_cq = (nvme_cq_entry_t *)PHYS_TO_VIRT(g_nvme.admin_cq_phys);
    memset(g_nvme.admin_cq, 0, 4096);

    // Initialize state
    g_nvme.admin_sq_tail = 0;
    g_nvme.admin_cq_head = 0;
    g_nvme.admin_cq_phase = 1;

    // Tell controller where the queues are
    // AQA: Admin Queue Attributes (ACQS in bits 27:16, ASQS in bits 11:0)
    uint32_t aqa = ((NVME_ADMIN_QUEUE_DEPTH - 1) << 16)
                 | ((NVME_ADMIN_QUEUE_DEPTH - 1) << 0);
    nvme_write32(NVME_REG_AQA, aqa);

    // ASQ: Admin Submission Queue base (64-bit physical)
    nvme_write64(NVME_REG_ASQ, (uint64_t)g_nvme.admin_sq_phys);

    // ACQ: Admin Completion Queue base (64-bit physical)
    nvme_write64(NVME_REG_ACQ, (uint64_t)g_nvme.admin_cq_phys);

    serial_log("NVMe: Admin queues configured.");
    serial_log_hex("  Admin SQ phys: ", g_nvme.admin_sq_phys);
    serial_log_hex("  Admin CQ phys: ", g_nvme.admin_cq_phys);
    serial_log_hex("  AQA:           ", aqa);

    return true;
}

// =============================================================================
// IDENTIFY CONTROLLER
// =============================================================================

static bool nvme_identify_controller(void) {
    // Allocate a 4KB buffer for the Identify data structure
    void *buf_phys = pmm_alloc_block();
    if (!buf_phys) {
        serial_log("NVMe: OOM for Identify Controller buffer");
        return false;
    }
    uint32_t buf_phys_addr = (uint32_t)(uintptr_t)buf_phys;
    uint8_t *buf = (uint8_t *)PHYS_TO_VIRT(buf_phys_addr);
    memset(buf, 0, 4096);

    // Build Identify Controller command
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    uint16_t cid = nvme_alloc_cid();
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.cid = cid;
    cmd.nsid = 0;
    cmd.prp1 = (uint64_t)buf_phys_addr;
    cmd.prp2 = 0;
    cmd.cdw10 = NVME_IDENTIFY_CTRL;  // CNS = 1

    nvme_submit_admin_cmd(&cmd);
    if (nvme_wait_admin_completion(cid) != 0) {
        serial_log("NVMe: Identify Controller command FAILED.");
        pmm_free_block(buf_phys);
        return false;
    }

    // Parse results
    // Offset 24-63: Serial Number (20 bytes), then Model Number (40 bytes)
    char serial[21] = {0};
    char model[41] = {0};
    memcpy(serial, buf + 4, 20);
    memcpy(model, buf + 24, 40);

    // Trim trailing spaces
    for (int i = 19; i >= 0 && serial[i] == ' '; i--) serial[i] = 0;
    for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = 0;

    serial_log("NVMe: Identify Controller:");
    serial_log("  Serial: ");
    serial_log(serial);
    serial_log("  Model:  ");
    serial_log(model);

    // MDTS (Maximum Data Transfer Size) at offset 77
    uint8_t mdts = buf[77];
    if (mdts > 0) {
        // max transfer = 2^mdts * (minimum page size)
        // minimum page size = 4096 bytes = 8 sectors
        g_nvme.max_transfer_blocks = (1 << mdts) * (4096 / g_nvme.lba_size);
        if (g_nvme.max_transfer_blocks == 0)
            g_nvme.max_transfer_blocks = 256;
    } else {
        g_nvme.max_transfer_blocks = 256; // No limit reported, use safe default
    }
    serial_log_hex("NVMe: Max transfer blocks: ", g_nvme.max_transfer_blocks);

    pmm_free_block(buf_phys);
    return true;
}

// =============================================================================
// CREATE I/O QUEUES
// =============================================================================

static bool nvme_create_io_queues(void) {
    // ── Allocate I/O queue memory ──
    void *io_sq_phys = pmm_alloc_block();
    void *io_cq_phys = pmm_alloc_block();
    if (!io_sq_phys || !io_cq_phys) {
        serial_log("NVMe: OOM allocating I/O queues!");
        return false;
    }

    g_nvme.io_sq_phys = (uint32_t)(uintptr_t)io_sq_phys;
    g_nvme.io_cq_phys = (uint32_t)(uintptr_t)io_cq_phys;
    g_nvme.io_sq = (nvme_sq_entry_t *)PHYS_TO_VIRT(g_nvme.io_sq_phys);
    g_nvme.io_cq = (nvme_cq_entry_t *)PHYS_TO_VIRT(g_nvme.io_cq_phys);
    memset(g_nvme.io_sq, 0, 4096);
    memset(g_nvme.io_cq, 0, 4096);

    g_nvme.io_sq_tail = 0;
    g_nvme.io_cq_head = 0;
    g_nvme.io_cq_phase = 1;

    // ── Create I/O Completion Queue (must be done FIRST) ──
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    uint16_t cid = nvme_alloc_cid();
    cmd.opcode = NVME_ADMIN_CREATE_IO_CQ;
    cmd.cid = cid;
    cmd.prp1 = (uint64_t)g_nvme.io_cq_phys;
    cmd.cdw10 = ((NVME_IO_QUEUE_DEPTH - 1) << 16) | 1;  // QID=1, size
    cmd.cdw11 = (1 << 0);  // Physically Contiguous (PC=1)
    // No interrupt vector (polled mode): IEN=0

    nvme_submit_admin_cmd(&cmd);
    if (nvme_wait_admin_completion(cid) != 0) {
        serial_log("NVMe: Create I/O CQ FAILED!");
        return false;
    }
    serial_log("NVMe: I/O Completion Queue created (QID=1).");

    // ── Create I/O Submission Queue ──
    memset(&cmd, 0, sizeof(cmd));
    cid = nvme_alloc_cid();
    cmd.opcode = NVME_ADMIN_CREATE_IO_SQ;
    cmd.cid = cid;
    cmd.prp1 = (uint64_t)g_nvme.io_sq_phys;
    cmd.cdw10 = ((NVME_IO_QUEUE_DEPTH - 1) << 16) | 1;  // QID=1, size
    cmd.cdw11 = (1 << 16) | (1 << 0);  // CQID=1 in bits 31:16, PC=1

    nvme_submit_admin_cmd(&cmd);
    if (nvme_wait_admin_completion(cid) != 0) {
        serial_log("NVMe: Create I/O SQ FAILED!");
        return false;
    }
    serial_log("NVMe: I/O Submission Queue created (QID=1).");

    return true;
}

// =============================================================================
// IDENTIFY NAMESPACE
// =============================================================================

static bool nvme_identify_namespace(uint32_t nsid) {
    void *buf_phys = pmm_alloc_block();
    if (!buf_phys) {
        serial_log("NVMe: OOM for Identify Namespace buffer");
        return false;
    }
    uint32_t buf_phys_addr = (uint32_t)(uintptr_t)buf_phys;
    uint8_t *buf = (uint8_t *)PHYS_TO_VIRT(buf_phys_addr);
    memset(buf, 0, 4096);

    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    uint16_t cid = nvme_alloc_cid();
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.cid = cid;
    cmd.nsid = nsid;
    cmd.prp1 = (uint64_t)buf_phys_addr;
    cmd.cdw10 = NVME_IDENTIFY_NS;  // CNS = 0

    nvme_submit_admin_cmd(&cmd);
    if (nvme_wait_admin_completion(cid) != 0) {
        serial_log("NVMe: Identify Namespace FAILED.");
        pmm_free_block(buf_phys);
        return false;
    }

    // Parse namespace data
    // NSZE (Namespace Size in LBAs) at offset 0, 8 bytes
    g_nvme.total_lbas = *(uint64_t *)(buf + 0);
    g_nvme.namespace_id = nsid;

    // FLBAS (Formatted LBA Size) at offset 26
    uint8_t flbas = buf[26];
    uint8_t lba_format_idx = flbas & 0x0F;

    // LBA Format descriptors start at offset 128, each is 4 bytes
    // LBADS (LBA Data Size, in power of 2) is bits 23:16
    uint32_t lba_format = *(uint32_t *)(buf + 128 + lba_format_idx * 4);
    uint8_t lbads = (lba_format >> 16) & 0xFF;
    g_nvme.lba_size = (1 << lbads);

    serial_log("NVMe: Namespace identified:");
    serial_log_hex("  NSID:       ", nsid);
    serial_log_hex("  Total LBAs: ", (uint32_t)(g_nvme.total_lbas & 0xFFFFFFFF));
    serial_log_hex("  LBA Size:   ", g_nvme.lba_size);
    serial_log_hex("  Capacity:   ", (uint32_t)((g_nvme.total_lbas * g_nvme.lba_size) / (1024 * 1024)));
    serial_log(" MB");

    pmm_free_block(buf_phys);
    return true;
}

// =============================================================================
// FULL INITIALIZATION
// =============================================================================

bool nvme_init(void) {
    serial_log("NVMe: Initializing...");
    memset(&g_nvme, 0, sizeof(g_nvme));
    memset(g_pending, 0, sizeof(g_pending));
    g_nvme.next_cid = 1;
    g_nvme.lba_size = 512;  // Default until Identify NS tells us otherwise

    // Step 1: Find NVMe on PCI
    if (!nvme_pci_probe()) return false;

    // Step 2: Map MMIO registers
    if (!nvme_map_mmio()) return false;

    // Step 3: Read capabilities
    uint64_t cap = nvme_read64(NVME_REG_CAP);
    g_nvme.max_queue_entries = NVME_CAP_MQES(cap) + 1;
    g_nvme.timeout_500ms = NVME_CAP_TO(cap);
    g_nvme.doorbell_stride = 4 << NVME_CAP_DSTRD(cap);

    serial_log_hex("NVMe: Max Queue Entries: ", g_nvme.max_queue_entries);
    serial_log_hex("NVMe: Doorbell Stride:   ", g_nvme.doorbell_stride);
    serial_log_hex("NVMe: Timeout (500ms):   ", g_nvme.timeout_500ms);

    // Step 4: Disable controller
    if (!nvme_disable_controller()) return false;

    // Step 5: Setup admin queues
    if (!nvme_setup_admin_queues()) return false;

    // Step 6: Enable controller
    if (!nvme_enable_controller()) return false;

    // Step 7: Identify namespace (before controller, because we need lba_size)
    if (!nvme_identify_namespace(1)) return false;

    // Step 8: Identify controller (uses lba_size for MDTS calc)
    if (!nvme_identify_controller()) return false;

    // Step 9: Create I/O queues
    if (!nvme_create_io_queues()) return false;

    // Step 10: Mask all interrupts (we use polling)
    nvme_write32(NVME_REG_INTMS, 0xFFFFFFFF);

    g_nvme.present = true;

    serial_log("NVMe: ════════════════════════════════════");
    serial_log("NVMe: Initialization COMPLETE!");
    serial_log_hex("NVMe: Capacity (MB): ",
        (uint32_t)((g_nvme.total_lbas * g_nvme.lba_size) / (1024 * 1024)));
    serial_log("NVMe: ════════════════════════════════════");

    return true;
}

// =============================================================================
// I/O SUBMISSION (INTERNAL)
// =============================================================================

static int nvme_submit_io_cmd(nvme_sq_entry_t *cmd) {
    memcpy(&g_nvme.io_sq[g_nvme.io_sq_tail], cmd, sizeof(nvme_sq_entry_t));
    g_nvme.io_sq_tail = (g_nvme.io_sq_tail + 1) % NVME_IO_QUEUE_DEPTH;
    nvme_ring_io_sq_doorbell();
    return 0;
}

static volatile int nvme_lock = 0;
static void nvme_acquire() {
    while (__sync_lock_test_and_set(&nvme_lock, 1)) {
        asm volatile("pause");
    }
}
static void nvme_release() {
    __sync_lock_release(&nvme_lock);
}

static int nvme_wait_io_completion(uint16_t expected_cid) {
    uint32_t timeout = 50000000;

    int yield_counter = 0;
    while (timeout--) {
        nvme_cq_entry_t *cqe = &g_nvme.io_cq[g_nvme.io_cq_head];
        uint8_t phase = cqe->status & 1;

        if (phase == g_nvme.io_cq_phase) {
            uint16_t status_code = (cqe->status >> 1) & 0x7FF;
            uint16_t completed_cid = cqe->cid;

            // Advance CQ head
            g_nvme.io_cq_head = (g_nvme.io_cq_head + 1) % NVME_IO_QUEUE_DEPTH;
            if (g_nvme.io_cq_head == 0) {
                g_nvme.io_cq_phase ^= 1;
            }
            nvme_ring_io_cq_doorbell();

            if (completed_cid != expected_cid) {
                // Store for async tracking if needed
                for (int i = 0; i < NVME_MAX_PENDING; i++) {
                    if (g_pending[i].active && g_pending[i].cid == completed_cid) {
                        g_pending[i].completed = true;
                        g_pending[i].status = status_code;
                        break;
                    }
                }
                continue;  // Keep looking for our CID
            }

            if (status_code != 0) {
                serial_log_hex("NVMe: I/O FAILED, status=", status_code);
                g_nvme.total_errors++;
                return -1;
            }
            return 0;
        }
        
        // Polling optimization: pause and yield
        if (++yield_counter >= 2000) {
            asm volatile("pause");
            kernel_yield();
            yield_counter = 0;
        }
    }

    serial_log("NVMe: I/O command TIMEOUT!");
    g_nvme.total_errors++;
    return -1;
}

// =============================================================================
// PRP LIST BUILDER
//
// For transfers larger than 8KB (2 pages), PRP2 must point to a PRP list
// (a page of uint64_t physical addresses for each subsequent page).
// For transfers <= 4KB: only PRP1 is needed.
// For transfers <= 8KB: PRP1 = first page, PRP2 = second page.
// For transfers > 8KB: PRP1 = first page, PRP2 = PRP list page.
// =============================================================================

static uint64_t g_prp_list_phys = 0;

static bool nvme_ensure_prp_list_page(void) {
    if (!g_prp_list_phys) {
        void *p = pmm_alloc_block();
        if (!p) {
            serial_log("NVMe: OOM allocating PRP list page");
            return false;
        }
        g_prp_list_phys = (uint64_t)(uintptr_t)p;
    }
    return true;
}

// =============================================================================
// SYNCHRONOUS READ
// =============================================================================

bool nvme_read_sectors(uint64_t lba, uint32_t count, void *buffer) {
    if (!g_nvme.present || !g_nvme.enabled) return false;
    if (count == 0) return true;

    uint32_t start_virt = (uint32_t)(uintptr_t)buffer;
    uint32_t total_bytes = count * g_nvme.lba_size;
    
    // Safety: Chunking to 512KB (matches 1 PRP page capacity)
    if (total_bytes > 511 * 4096) {
        uint32_t sectors_per_chunk = (511 * 4096) / g_nvme.lba_size;
        uint8_t *ptr = (uint8_t *)buffer;
        while (count > 0) {
            uint32_t chunk = (count > sectors_per_chunk) ? sectors_per_chunk : count;
            if (!nvme_read_sectors(lba, chunk, ptr)) return false;
            lba += chunk;
            count -= chunk;
            ptr += chunk * g_nvme.lba_size;
            
            // Socially Responsible Back-off: Ensure GUI core isn't starved
            for (volatile int i = 0; i < 2000; i++) { asm volatile("pause"); }
            
            kernel_yield(); // Added yield during large transfer
        }
        return true;
    }

    nvme_acquire(); // Use spinlock instead of CLI
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    uint16_t cid = nvme_alloc_cid();
    cmd.opcode = NVME_IO_READ;
    cmd.cid = cid;
    cmd.nsid = g_nvme.namespace_id;
    
    // Page boundary calculation for PRP entries
    uint32_t page_offset = start_virt & 4095;
    uint32_t first_page_max = 4096 - page_offset;
    
    cmd.prp1 = paging_get_phys_generic(start_virt);

    if (total_bytes <= first_page_max) {
        cmd.prp2 = 0;
    } else {
        uint32_t bytes_left = total_bytes - first_page_max;
        uint32_t extra_pages = (bytes_left + 4095) / 4096;
        
        if (extra_pages == 1) {
            cmd.prp2 = paging_get_phys_generic(start_virt + first_page_max);
        } else {
            if (!nvme_ensure_prp_list_page()) {
                nvme_release();
                return false;
            }
            uint64_t *prp_list = (uint64_t *)pae_map_window(g_prp_list_phys, 3);
            for (uint32_t i = 0; i < extra_pages; i++) {
                prp_list[i] = paging_get_phys_generic(start_virt + first_page_max + (i * 4096));
            }
            pae_unmap_window(3);
            cmd.prp2 = g_prp_list_phys;
        }
    }

    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd.cdw11 = (uint32_t)((lba >> 32) & 0xFFFFFFFF);
    cmd.cdw12 = count - 1;

    nvme_submit_io_cmd(&cmd);
    int result = nvme_wait_io_completion(cid);

    nvme_release();

    if (result == 0) {
        g_nvme.total_reads++;
        return true;
    }
    return false;
}

// =============================================================================
// SYNCHRONOUS WRITE
// =============================================================================

bool nvme_write_sectors(uint64_t lba, uint32_t count, const void *buffer) {
    if (!g_nvme.present || !g_nvme.enabled) return false;
    if (count == 0) return true;

    uint32_t start_virt = (uint32_t)(uintptr_t)buffer;
    uint32_t total_bytes = count * g_nvme.lba_size;

    // Chunking to 512KB
    if (total_bytes > 511 * 4096) {
        uint32_t sectors_per_chunk = (511 * 4096) / g_nvme.lba_size;
        uint8_t *ptr = (uint8_t *)buffer;
        while (count > 0) {
            uint32_t chunk = (count > sectors_per_chunk) ? sectors_per_chunk : count;
            if (!nvme_write_sectors(lba, chunk, ptr)) return false;
            lba += chunk;
            count -= chunk;
            ptr += chunk * g_nvme.lba_size;
            kernel_yield(); // Added yield during large transfer
        }
        return true;
    }

    nvme_acquire(); // Use spinlock instead of CLI

    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    uint16_t cid = nvme_alloc_cid();
    cmd.opcode = NVME_IO_WRITE;
    cmd.cid = cid;
    cmd.nsid = g_nvme.namespace_id;
    
    uint32_t page_offset = start_virt & 4095;
    uint32_t first_page_max = 4096 - page_offset;
    
    cmd.prp1 = paging_get_phys_generic(start_virt);

    if (total_bytes <= first_page_max) {
        cmd.prp2 = 0;
    } else {
        uint32_t bytes_left = total_bytes - first_page_max;
        uint32_t extra_pages = (bytes_left + 4095) / 4096;

        if (extra_pages == 1) {
            cmd.prp2 = paging_get_phys_generic(start_virt + first_page_max);
        } else {
            if (!nvme_ensure_prp_list_page()) {
                nvme_release();
                return false;
            }
            uint64_t *prp_list = (uint64_t *)pae_map_window(g_prp_list_phys, 3);
            for (uint32_t i = 0; i < extra_pages; i++) {
                prp_list[i] = paging_get_phys_generic(start_virt + first_page_max + (i * 4096));
            }
            pae_unmap_window(3);
            cmd.prp2 = g_prp_list_phys;
        }
    }

    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd.cdw11 = (uint32_t)((lba >> 32) & 0xFFFFFFFF);
    cmd.cdw12 = count - 1;

    nvme_submit_io_cmd(&cmd);
    int result = nvme_wait_io_completion(cid);

    nvme_release();

    if (result == 0) {
        g_nvme.total_writes++;
        return true;
    }
    return false;
}

// =============================================================================
// ASYNC I/O
// =============================================================================

int nvme_read_async(uint64_t lba, uint32_t count, void *buffer) {
    if (!g_nvme.present || !g_nvme.enabled) return -1;

    // Find free pending slot
    int slot = -1;
    for (int i = 0; i < NVME_MAX_PENDING; i++) {
        if (!g_pending[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        serial_log("NVMe: No free async slots!");
        return -1;
    }

    // Protection against concurrent access
    uint32_t eflags;
    asm volatile("pushf; pop %0; cli" : "=r"(eflags));

    uint64_t buf_phys = paging_get_phys_generic((uint32_t)(uintptr_t)buffer);
    uint32_t total_bytes = count * g_nvme.lba_size;
    uint32_t total_pages = (total_bytes + 4095) / 4096;

    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    uint16_t cid = nvme_alloc_cid();
    cmd.opcode = NVME_IO_READ;
    cmd.cid = cid;
    cmd.nsid = g_nvme.namespace_id;
    cmd.prp1 = buf_phys;

    if (total_pages <= 1) {
        cmd.prp2 = 0;
    } else if (total_pages == 2) {
        cmd.prp2 = (uint64_t)(buf_phys + 4096);
    } else {
        if (!nvme_ensure_prp_list_page()) {
            if (eflags & 0x200) asm volatile("sti");
            return -1;
        }
        uint64_t *prp_list = (uint64_t *)pae_map_window(g_prp_list_phys, 3);
        for (uint32_t i = 1; i < total_pages; i++) {
            // Note: This logic assumes physically contiguous buffer if it's async? 
            // Better to translate each page if non-contiguous.
            prp_list[i - 1] = paging_get_phys_generic((uint32_t)(uintptr_t)buffer + (i * 4096));
        }
        pae_unmap_window(3);
        cmd.prp2 = g_prp_list_phys;
    }

    if (eflags & 0x200) asm volatile("sti");

    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd.cdw11 = (uint32_t)((lba >> 32) & 0xFFFFFFFF);
    cmd.cdw12 = count - 1;

    // Register in pending table
    g_pending[slot].cid = cid;
    g_pending[slot].active = true;
    g_pending[slot].completed = false;
    g_pending[slot].status = 0;
    g_pending[slot].buffer = buffer;

    nvme_submit_io_cmd(&cmd);
    return (int)cid;
}

int nvme_poll_completion(int cid) {
    // First drain any new completions
    nvme_process_completions();

    // Check if our CID completed
    for (int i = 0; i < NVME_MAX_PENDING; i++) {
        if (g_pending[i].active && g_pending[i].cid == (uint16_t)cid) {
            if (g_pending[i].completed) {
                bool ok = (g_pending[i].status == 0);
                g_pending[i].active = false;  // Free slot
                if (ok) {
                    g_nvme.total_reads++;
                    return 1;
                }
                g_nvme.total_errors++;
                return -1;
            }
            return 0;  // Still pending
        }
    }
    return -1;  // CID not found
}

void nvme_process_completions(void) {
    if (!g_nvme.present) return;

    // Drain all available completions
    for (int safety = 0; safety < NVME_IO_QUEUE_DEPTH; safety++) {
        nvme_cq_entry_t *cqe = &g_nvme.io_cq[g_nvme.io_cq_head];
        uint8_t phase = cqe->status & 1;

        if (phase != g_nvme.io_cq_phase) break;  // No more new completions

        uint16_t status_code = (cqe->status >> 1) & 0x7FF;
        uint16_t completed_cid = cqe->cid;

        // Find in pending table
        for (int i = 0; i < NVME_MAX_PENDING; i++) {
            if (g_pending[i].active && g_pending[i].cid == completed_cid) {
                g_pending[i].completed = true;
                g_pending[i].status = status_code;
                break;
            }
        }

        // Advance CQ head
        g_nvme.io_cq_head = (g_nvme.io_cq_head + 1) % NVME_IO_QUEUE_DEPTH;
        if (g_nvme.io_cq_head == 0) {
            g_nvme.io_cq_phase ^= 1;
        }
    }

    // Update doorbell after draining
    nvme_ring_io_cq_doorbell();
}

// =============================================================================
// SHUTDOWN
// =============================================================================

void nvme_shutdown(void) {
    if (!g_nvme.present) return;

    serial_log("NVMe: Shutting down...");

    // Set Shutdown Notification (CC.SHN = 01b: Normal)
    uint32_t cc = nvme_read32(NVME_REG_CC);
    cc &= ~(3 << 14);     // Clear SHN bits
    cc |= (1 << 14);      // SHN = 01 (Normal shutdown)
    nvme_write32(NVME_REG_CC, cc);

    // Wait for CSTS.SHST = 10b (Shutdown complete)
    uint32_t timeout = 50000000;
    while (timeout--) {
        uint32_t csts = nvme_read32(NVME_REG_CSTS);
        if (((csts >> 2) & 3) == 2) {
            serial_log("NVMe: Shutdown complete.");
            break;
        }
    }

    // Free queue memory
    if (g_nvme.admin_sq_phys) pmm_free_block((void *)(uintptr_t)g_nvme.admin_sq_phys);
    if (g_nvme.admin_cq_phys) pmm_free_block((void *)(uintptr_t)g_nvme.admin_cq_phys);
    if (g_nvme.io_sq_phys) pmm_free_block((void *)(uintptr_t)g_nvme.io_sq_phys);
    if (g_nvme.io_cq_phys) pmm_free_block((void *)(uintptr_t)g_nvme.io_cq_phys);
    if (g_prp_list_phys) pmm_free_block((void *)(uintptr_t)g_prp_list_phys);

    g_nvme.present = false;
    g_nvme.enabled = false;
    serial_log("NVMe: Resources freed.");
}

// =============================================================================
// INFO / QUERIES
// =============================================================================

bool nvme_is_present(void) {
    return g_nvme.present && g_nvme.enabled;
}

uint64_t nvme_get_capacity_bytes(void) {
    return g_nvme.total_lbas * g_nvme.lba_size;
}

uint32_t nvme_get_lba_size(void) {
    return g_nvme.lba_size;
}

void nvme_print_info(void) {
    serial_log("NVMe: ═══════ Drive Info ═══════");
    serial_log_hex("  Present:     ", g_nvme.present);
    serial_log_hex("  NSID:        ", g_nvme.namespace_id);
    serial_log_hex("  LBA Size:    ", g_nvme.lba_size);
    serial_log_hex("  Total LBAs:  ", (uint32_t)(g_nvme.total_lbas & 0xFFFFFFFF));
    serial_log_hex("  Capacity MB: ",
        (uint32_t)((g_nvme.total_lbas * g_nvme.lba_size) / (1024 * 1024)));
    serial_log_hex("  Reads:       ", g_nvme.total_reads);
    serial_log_hex("  Writes:      ", g_nvme.total_writes);
    serial_log_hex("  Errors:      ", g_nvme.total_errors);
    serial_log("NVMe: ═════════════════════════");
}

// =============================================================================
// IRQ HANDLER (optional — currently using polled mode)
// =============================================================================

void nvme_irq_handler(registers_t *regs) {
    (void)regs;
    nvme_process_completions();
    lapic_eoi();
}

} // extern "C"
