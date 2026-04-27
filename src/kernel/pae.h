#pragma once
#include <stdint.h>
#include "../include/memory_map.h"

// ============================================================
// PAE PAGING HEADER - Retro-OS (NX-Free Edition)
// 3-Level Paging: PDPT -> Page Directory -> Page Table
// 36-bit Physical Addressing (up to 64 GB RAM)
//
// NOTE: NX bit is NOT supported on this target CPU.
//       This implementation compensates via GDT segment limits
//       and strict page-level read-only permissions.
// ============================================================

// === PAE Virtual Address Decomposition ===
#define PAE_PDPT_INDEX(va)   (((uint32_t)(va) >> 30) & 0x3)
#define PAE_PD_INDEX(va)    (((uint32_t)(va) >> 21) & 0x1FF)
#define PAE_PT_INDEX(va)    (((uint32_t)(va) >> 12) & 0x1FF)
#define PAE_PAGE_OFFSET(va) ((uint32_t)(va) & 0xFFF)

// === Entry Flags (NX Is Explicitly Excluded) ===
#define PAE_FLAG_PRESENT     (1ULL << 0)
#define PAE_FLAG_WRITABLE    (1ULL << 1)
#define PAE_FLAG_USER        (1ULL << 2)    // Ring 2/3 accessible
#define PAE_FLAG_PWT         (1ULL << 3)
#define PAE_FLAG_PCD         (1ULL << 4)
#define PAE_FLAG_ACCESSED    (1ULL << 5)
#define PAE_FLAG_DIRTY       (1ULL << 6)
#define PAE_FLAG_PAGESIZE    (1ULL << 7)    // 2MB page in PD
#define PAE_FLAG_GLOBAL      (1ULL << 8)

// *** NX bit (bit 63) MUST be zero to avoid Reserved Bit Faults ***
#define PAE_FLAG_NX          0ULL 

// === Physical Address Mask ===
#define PAE_ADDR_MASK        0x0000000FFFFFF000ULL
#define PAE_RESERVED_MASK    0xFFF0000000000000ULL

// === Ring 2 Permissions ===
#define PAE_RING2_CODE       (PAE_FLAG_PRESENT | PAE_FLAG_USER)
#define PAE_RING2_DATA       (PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE | PAE_FLAG_USER)
#define PAE_RING2_STACK      (PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE | PAE_FLAG_USER)
#define PAE_RING2_WEIGHTS    (PAE_FLAG_PRESENT | PAE_FLAG_USER) // Read-Only

// === Structure Sizes ===
#define PAE_PDPT_ENTRIES     4
#define PAE_PD_ENTRIES       512
#define PAE_PT_ENTRIES       512

typedef struct __attribute__((aligned(32))) {
    uint64_t entries[PAE_PDPT_ENTRIES];
} pae_pdpt_t;

typedef struct __attribute__((aligned(4096))) {
    uint64_t entries[PAE_PD_ENTRIES];
} pae_page_directory_t;

typedef struct __attribute__((aligned(4096))) {
    uint64_t entries[PAE_PT_ENTRIES];
} pae_page_table_t;

#define KERNEL_PT_COUNT 512

// === Sliding Window ===
#define PAE_SLIDING_WINDOW_BASE   0xFFE00000
#define PAE_SLIDING_WINDOW_SLOTS  4

#ifdef __cplusplus
extern "C" {
#endif

// Core lifecycle
bool pae_check_cpu_support(void);
void pae_init(void);

// Page mapping
void pae_map_page(uint32_t virtual_addr, uint64_t physical_addr, uint64_t flags);
void pae_map_range(uint32_t virtual_addr, uint64_t physical_addr, uint32_t page_count, uint64_t flags);
void pae_unmap_page(uint32_t virtual_addr);
uint64_t pae_get_physical(uint32_t virtual_addr);

// High-memory windows
void* pae_map_window(uint64_t phys_addr_64, int slot);
void  pae_unmap_window(int slot);

// ASM wrappers
void pae_enable(uint32_t pdpt_phys, bool nx_supported); // nx_supported will be false
void pae_flush_tlb(void);
void pae_invlpg(uint32_t virtual_addr);

// Security audits
void pae_verify_no_nx(void);

extern uint32_t g_pdpt_phys_addr;
extern bool g_pae_active;

#ifdef __cplusplus
}
#endif
