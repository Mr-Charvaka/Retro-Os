#ifndef NVME_H
#define NVME_H

#include "../include/types.h"
#include "../include/isr.h"


// =============================================================================
// NVMe Driver — Retro-OS Flagship Implementation
//
// Implements NVM Express 1.0 over PCIe for high-speed storage access.
// Designed for AI model weight streaming via demand paging.
//
// Architecture:
//   - 1 Admin Queue Pair (submission + completion)
//   - 1 I/O Queue Pair (submission + completion)
//   - Polled completion (no MSI/MSI-X required, optional IRQ)
//   - Synchronous + Asynchronous read support
//   - Integrates with existing VFS/FAT32 and block abstraction
// =============================================================================

// === PCI Class Codes ===
#define NVME_PCI_CLASS          0x01    // Mass Storage Controller
#define NVME_PCI_SUBCLASS       0x08    // Non-Volatile Memory Controller
#define NVME_PCI_PROG_IF        0x02    // NVM Express

// === NVMe Controller Registers (BAR0 MMIO offsets) ===
#define NVME_REG_CAP            0x0000  // Controller Capabilities (64-bit)
#define NVME_REG_VS             0x0008  // Version (32-bit)
#define NVME_REG_INTMS          0x000C  // Interrupt Mask Set (32-bit)
#define NVME_REG_INTMC          0x0010  // Interrupt Mask Clear (32-bit)
#define NVME_REG_CC             0x0014  // Controller Configuration (32-bit)
#define NVME_REG_CSTS           0x001C  // Controller Status (32-bit)
#define NVME_REG_NSSR           0x0020  // NVM Subsystem Reset (optional)
#define NVME_REG_AQA            0x0024  // Admin Queue Attributes (32-bit)
#define NVME_REG_ASQ            0x0028  // Admin SQ Base Address (64-bit)
#define NVME_REG_ACQ            0x0030  // Admin CQ Base Address (64-bit)

// === Controller Configuration (CC) Fields ===
#define NVME_CC_EN              (1 << 0)    // Enable
#define NVME_CC_CSS_NVM         (0 << 4)    // NVM Command Set
#define NVME_CC_MPS_4K          (0 << 7)    // Memory Page Size = 4KB
#define NVME_CC_AMS_RR          (0 << 11)   // Arbitration: Round Robin
#define NVME_CC_IOSQES_64       (6 << 16)   // I/O SQ Entry Size = 2^6 = 64
#define NVME_CC_IOCQES_16       (4 << 20)   // I/O CQ Entry Size = 2^4 = 16

// === Controller Status (CSTS) Fields ===
#define NVME_CSTS_RDY           (1 << 0)    // Ready
#define NVME_CSTS_CFS           (1 << 1)    // Controller Fatal Status
#define NVME_CSTS_SHST_MASK     (3 << 2)    // Shutdown Status

// === CAP Register Fields ===
#define NVME_CAP_MQES(cap)      ((uint16_t)((cap) & 0xFFFF))           // Max Queue Entries Supported
#define NVME_CAP_TO(cap)        ((uint8_t)(((cap) >> 24) & 0xFF))     // Timeout (in 500ms units)
#define NVME_CAP_DSTRD(cap)     ((uint8_t)(((cap) >> 32) & 0xF))     // Doorbell Stride
#define NVME_CAP_MPSMIN(cap)    ((uint8_t)(((cap) >> 48) & 0xF))     // Min Memory Page Size
#define NVME_CAP_MPSMAX(cap)    ((uint8_t)(((cap) >> 52) & 0xF))     // Max Memory Page Size

// === Admin Command Opcodes ===
#define NVME_ADMIN_DELETE_IO_SQ  0x00
#define NVME_ADMIN_CREATE_IO_SQ  0x01
#define NVME_ADMIN_GET_LOG_PAGE  0x02
#define NVME_ADMIN_DELETE_IO_CQ  0x04
#define NVME_ADMIN_CREATE_IO_CQ  0x05
#define NVME_ADMIN_IDENTIFY      0x06
#define NVME_ADMIN_ABORT         0x08
#define NVME_ADMIN_SET_FEATURES  0x09
#define NVME_ADMIN_GET_FEATURES  0x0A

// === I/O Command Opcodes ===
#define NVME_IO_FLUSH            0x00
#define NVME_IO_WRITE            0x01
#define NVME_IO_READ             0x02

// === Identify CNS Values ===
#define NVME_IDENTIFY_NS         0x00   // Identify Namespace
#define NVME_IDENTIFY_CTRL       0x01   // Identify Controller

// === Queue Depths ===
#define NVME_ADMIN_QUEUE_DEPTH   16
#define NVME_IO_QUEUE_DEPTH      64

// === MMIO Virtual Base (must not conflict with memory_map.h) ===
// Using a region above PAE KV window but below framebuffer
#define NVME_MMIO_VIRT_BASE      0xEF000000
#define NVME_MMIO_MAP_SIZE       0x00010000  // 64KB should cover BAR0

// =============================================================================
// NVMe Submission Queue Entry (64 bytes — hardware defined)
// =============================================================================
typedef struct __attribute__((packed)) {
    // Dword 0
    uint32_t opcode   : 8;     // Command opcode
    uint32_t fuse     : 2;     // Fused operation
    uint32_t rsvd0    : 4;     // Reserved
    uint32_t psdt     : 2;     // PRP or SGL
    uint32_t cid      : 16;    // Command Identifier

    // Dword 1
    uint32_t nsid;              // Namespace Identifier

    // Dwords 2-3
    uint32_t cdw2;
    uint32_t cdw3;

    // Dwords 4-5: Metadata Pointer
    uint64_t mptr;

    // Dwords 6-9: Data Pointer (PRP1 + PRP2)
    uint64_t prp1;
    uint64_t prp2;

    // Dwords 10-15: Command-specific
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_sq_entry_t;

// Verify hardware expects exactly 64 bytes
static_assert(sizeof(nvme_sq_entry_t) == 64,
    "NVMe SQ entry must be exactly 64 bytes");

// =============================================================================
// NVMe Completion Queue Entry (16 bytes — hardware defined)
// =============================================================================
typedef struct __attribute__((packed)) {
    uint32_t cdw0;          // Command-specific result
    uint32_t reserved;

    uint16_t sq_head;       // SQ Head Pointer (controller's view)
    uint16_t sq_id;         // SQ Identifier

    uint16_t cid;           // Command Identifier (matches SQ entry)
    uint16_t status;        // Status Field:
                            //   bit 0    = Phase Tag (P)
                            //   bits 1-8 = Status Code
                            //   bits 9-11 = Status Code Type
                            //   bits 12-14 = reserved
                            //   bit 15 = Do Not Retry
} nvme_cq_entry_t;

static_assert(sizeof(nvme_cq_entry_t) == 16,
    "NVMe CQ entry must be exactly 16 bytes");

// =============================================================================
// NVMe Device State
// =============================================================================
typedef struct {
    // PCI location
    uint8_t  pci_bus;
    uint8_t  pci_slot;
    uint8_t  pci_func;

    // MMIO
    uint32_t bar0_phys;                 // Physical BAR0 address
    volatile uint8_t *mmio;             // Virtual MMIO base pointer

    // Controller capabilities
    uint32_t doorbell_stride;           // Bytes between doorbells (4 << DSTRD)
    uint16_t max_queue_entries;         // Max entries per queue
    uint8_t  timeout_500ms;             // CAP.TO in 500ms units

    // Admin Queue Pair
    nvme_sq_entry_t *admin_sq;          // Virtual pointer to Admin SQ
    nvme_cq_entry_t *admin_cq;          // Virtual pointer to Admin CQ
    uint64_t admin_sq_phys;             // Physical address of Admin SQ
    uint64_t admin_cq_phys;             // Physical address of Admin CQ
    uint16_t admin_sq_tail;             // Next slot to write in SQ
    uint16_t admin_cq_head;             // Next slot to read in CQ
    uint8_t  admin_cq_phase;            // Expected phase bit

    // I/O Queue Pair (Queue ID 1)
    nvme_sq_entry_t *io_sq;
    nvme_cq_entry_t *io_cq;
    uint64_t io_sq_phys;
    uint64_t io_cq_phys;
    uint16_t io_sq_tail;
    uint16_t io_cq_head;
    uint8_t  io_cq_phase;

    // Namespace info
    uint32_t namespace_id;              // Active NSID (usually 1)
    uint64_t total_lbas;                // Total logical blocks
    uint32_t lba_size;                  // Bytes per LBA (usually 512)
    uint16_t max_transfer_blocks;       // Max blocks per single read/write

    // Command tracking
    uint16_t next_cid;                  // Next command identifier

    // Status
    bool     present;                   // Controller found and initialized
    bool     enabled;                   // Controller enabled and ready

    // Stats
    uint32_t total_reads;
    uint32_t total_writes;
    uint32_t total_errors;
} nvme_device_t;

// =============================================================================
// Async I/O Tracking
// =============================================================================
#define NVME_MAX_PENDING 32

typedef struct {
    uint16_t cid;           // Command ID
    bool     active;        // Slot in use
    bool     completed;     // Completion received
    uint16_t status;        // Completion status
    void    *buffer;        // User buffer (for callback use)
} nvme_pending_t;

// =============================================================================
// Public API
// =============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// --- Lifecycle ---
bool nvme_init(void);                   // PCI probe + full controller init
void nvme_shutdown(void);               // Graceful shutdown

// --- Synchronous I/O ---
// Read 'count' LBAs starting from 'lba' into 'buffer'
// buffer must be physically contiguous and accessible via PHYS_TO_VIRT
// Returns true on success
bool nvme_read_sectors(uint64_t lba, uint32_t count, void *buffer);

// Write 'count' LBAs from 'buffer' to disk starting at 'lba'
bool nvme_write_sectors(uint64_t lba, uint32_t count, const void *buffer);

// --- Asynchronous I/O ---
// Submit a read without waiting. Returns command ID, or -1 on failure.
int nvme_read_async(uint64_t lba, uint32_t count, void *buffer);

// Poll for completion of a specific command. Returns:
//   1  = completed successfully
//   0  = still pending
//   -1 = completed with error
int nvme_poll_completion(int cid);

// Poll and retire all completed commands (call from main loop or IRQ)
void nvme_process_completions(void);

// --- Info ---
bool nvme_is_present(void);
uint64_t nvme_get_capacity_bytes(void);
uint32_t nvme_get_lba_size(void);
void nvme_print_info(void);

// --- IRQ Handler (optional — polled mode works without it) ---
void nvme_irq_handler(registers_t *regs);

#ifdef __cplusplus
}
#endif

#endif // NVME_H
