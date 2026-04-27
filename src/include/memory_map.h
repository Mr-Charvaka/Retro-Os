// =================================================================
// memory_map.h — Retro-OS System Memory Map
// =================================================================
// Master reference for ALL memory region addresses.
// EVERY subsystem that allocates or maps memory MUST use these
// constants. Do NOT use magic numbers anywhere else.
//
// Mapping convention (higher-half kernel):
//   virtual_address = physical_address + KERNEL_VIRT_OFFSET
//
// Last updated: Ring 2 territory + underflow guard + GDT selectors
// =================================================================

#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <stdint.h>

// =================================================================
// SECTION 1: GLOBAL CONSTANTS
// =================================================================

#ifndef KERNEL_VIRTUAL_BASE
#define KERNEL_VIRT_OFFSET      0xC0000000
#else
#define KERNEL_VIRT_OFFSET      KERNEL_VIRTUAL_BASE
#endif

#define PAGE_SIZE               0x00001000  // 4 KB
#define TOTAL_RAM               0x40000000  // 1 GB

// =================================================================
// SECTION 2: KERNEL REGIONS (EXISTING — DO NOT CHANGE)
// =================================================================

// --- Kernel Binary ---
#define KERNEL_PHYS_START       0x00008000
#define KERNEL_VIRT_START       0xC0008000
// KERNEL_VIRT_END is provided by linker symbol _kernel_end

// --- Placement Heap (early boot allocator) ---
#define PLACEMENT_PHYS_START    0x00C00000
#define PLACEMENT_VIRT_START    0xC0C00000
#define PLACEMENT_SIZE          0x00400000  // 4 MB

// --- Kernel Heap (kmalloc) ---
#define KHEAP_PHYS_START        0x01000000
#define KHEAP_VIRT_START        0xC1000000
#define KHEAP_SIZE              0x20000000  // 512 MB
#define KHEAP_PHYS_END          (KHEAP_PHYS_START + KHEAP_SIZE)  // 0x21000000
#define KHEAP_VIRT_END          (KHEAP_VIRT_START + KHEAP_SIZE)  // 0xE1000000

// --- Kernel Boot Stack (in .bss, set up by kernel_entry.asm) ---
#define KSTACK_SIZE             0x00010000  // 64 KB
// Actual address is linker-determined (stack_top symbol)

// --- Per-Process Kernel Stack ---
#define PROC_KSTACK_SIZE        0x00001000  // 4 KB (allocated from kheap)

// =================================================================
// SECTION 3: USER SPACE REGIONS (EXISTING — DO NOT CHANGE)
// =================================================================

#define USER_CODE_DEFAULT       0x08048000  // Standard ELF load address
#define USER_STACK_TOP          0xB0000000  // Stack grows downward

// =================================================================
// SECTION 4: RING 2 TERRITORY
// =================================================================
//
// Ring 2 (AI Brain) occupies a dedicated region in the kernel
// virtual address space, above the kernel heap and below the
// framebuffer. All pages are Supervisor (U/S=0) so Ring 3
// cannot access them.
//
// Layout (virtual, bottom to top):
//   [underflow guard 64K][Stack 256K][overflow guard 768K]
//   [Code 1M][Heap 16M][Model 80M][Work 16M][end guard]
//

// --- Overall Region ---
#define RING2_PHYS_START        (KHEAP_PHYS_END + 0x00010000)
#define RING2_VIRT_START        (KHEAP_VIRT_END + 0x00010000)
#define RING2_REGION_SIZE       0x08000000  // 128 MB total region
#define RING2_PHYS_END          (RING2_PHYS_START + RING2_REGION_SIZE)
#define RING2_VIRT_END          (RING2_VIRT_START + RING2_REGION_SIZE)

// --- Ring 2 Stack ---
#define RING2_STACK_PHYS        (RING2_PHYS_START)
#define RING2_STACK_VIRT        (RING2_VIRT_START)
#define RING2_STACK_SIZE        0x00040000  // 256 KB
#define RING2_STACK_TOP_PHYS    (RING2_STACK_PHYS + RING2_STACK_SIZE)
#define RING2_STACK_TOP_VIRT    (RING2_STACK_VIRT + RING2_STACK_SIZE)

// --- Stack Underflow Guard (BELOW stack) ---
#define RING2_STACK_UNDERFLOW_GUARD_SIZE  0x00010000  // 64 KB
#define RING2_STACK_UNDERFLOW_GUARD_VIRT  (RING2_STACK_VIRT - RING2_STACK_UNDERFLOW_GUARD_SIZE)

// --- Stack Overflow Guard (ABOVE stack) ---
#define RING2_STACK_GUARD_VIRT  (RING2_STACK_VIRT + RING2_STACK_SIZE)
#define RING2_STACK_GUARD_SIZE  0x000C0000  // 768 KB unmapped gap

// --- Ring 2 Code ---
#define RING2_CODE_PHYS         (RING2_PHYS_START + 0x00100000)
#define RING2_CODE_VIRT         (RING2_VIRT_START + 0x00100000)
#define RING2_CODE_SIZE         0x00100000  // 1 MB

// --- Ring 2 Heap ---
#define RING2_HEAP_PHYS         (RING2_PHYS_START + 0x00200000)
#define RING2_HEAP_VIRT         (RING2_VIRT_START + 0x00200000)
#define RING2_HEAP_SIZE         0x01000000  // 16 MB

// --- Ring 2 Model Weights ---
#define RING2_MODEL_PHYS        (RING2_PHYS_START + 0x01200000)
#define RING2_MODEL_VIRT        (RING2_VIRT_START + 0x01200000)
#define RING2_MODEL_SIZE        0x05000000  // 80 MB

// --- Ring 2 Working Memory ---
#define RING2_WORK_PHYS         (RING2_PHYS_START + 0x06200000)
#define RING2_WORK_VIRT         (RING2_VIRT_START + 0x06200000)
#define RING2_WORK_SIZE         0x01000000  // 16 MB

// --- End Guard Zone (UNMAPPED) ---
#define RING2_END_GUARD_VIRT    (RING2_VIRT_START + 0x07200000)
#define RING2_END_GUARD_SIZE    0x00E00000

// =================================================================
// SECTION 4B: RING 2 GDT SELECTORS
// =================================================================
// These MUST match the GDT entries defined in gdt.cpp.
// Selector = (GDT_index * 8) | RPL
//
// GDT Layout:
//   [0] Null  [1] R0 Code  [2] R0 Data  [3] R3 Code
//   [4] R3 Data  [5] TSS  [6] R2 Code  [7] R2 Data
//   [8] Call Gate (reserved for Step 1E)
//

#define GDT_ENTRY_COUNT         10

#define RING0_CODE_SEL          0x08    // (1 * 8) | 0
#define RING0_DATA_SEL          0x10    // (2 * 8) | 0
#define RING3_CODE_SEL          0x1B    // (3 * 8) | 3
#define RING3_DATA_SEL          0x23    // (4 * 8) | 3
#define TSS_SEL                 0x28    // (5 * 8) | 0

#define RING2_CODE_SEL          0x32    // (6 * 8) | 2
#define RING2_DATA_SEL          0x3A    // (7 * 8) | 2
#define BRAIN_GATE_SEL          0x43    // (8 * 8) | 3  (Step 1E)

// =================================================================
// SECTION 5: FRAMEBUFFER (EXISTING — DO NOT CHANGE)
// =================================================================

#define FRAMEBUFFER_SIZE        0x01000000  // 16 MB mapping

// =================================================================
// SECTION 6: COMPILE-TIME OVERLAP CHECKS
// =================================================================

#ifdef __cplusplus
#define STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

// Kernel heap must end before Ring 2 starts
STATIC_ASSERT(KHEAP_VIRT_END <= RING2_STACK_UNDERFLOW_GUARD_VIRT,
    "FATAL: Kernel heap overlaps Ring 2 territory!");

// Ring 2 must end before framebuffer region
STATIC_ASSERT(RING2_VIRT_END < 0xFD000000,
    "FATAL: Ring 2 territory overlaps framebuffer region!");

// Ring 2 must fit within physical RAM
STATIC_ASSERT(RING2_PHYS_END < TOTAL_RAM,
    "FATAL: Ring 2 territory exceeds physical RAM!");

// Ring 2 stack must not overlap Ring 2 code
STATIC_ASSERT(RING2_STACK_TOP_VIRT <= RING2_STACK_GUARD_VIRT,
    "FATAL: Ring 2 stack overflows into guard zone constants!");
STATIC_ASSERT((RING2_STACK_GUARD_VIRT + RING2_STACK_GUARD_SIZE) <= RING2_CODE_VIRT,
    "FATAL: Ring 2 stack guard overlaps Ring 2 code!");

// Stack underflow guard must not overlap kernel heap
STATIC_ASSERT(RING2_STACK_UNDERFLOW_GUARD_VIRT >= KHEAP_VIRT_END,
    "FATAL: Ring 2 stack underflow guard overlaps kernel heap!");

// Stack underflow guard must end exactly at stack start
STATIC_ASSERT((RING2_STACK_UNDERFLOW_GUARD_VIRT + RING2_STACK_UNDERFLOW_GUARD_SIZE) == RING2_STACK_VIRT,
    "FATAL: Ring 2 stack underflow guard not contiguous with stack!");

// User stack must not reach into kernel virtual space
STATIC_ASSERT(USER_STACK_TOP <= KERNEL_VIRT_OFFSET,
    "FATAL: User stack top is in kernel virtual address space!");

// =================================================================
// SECTION 7: E820 MEMORY MAP CONSTANTS
// =================================================================

// Where the bootloader stores E820 data (physical addresses)
#define E820_BOOT_DATA_PHYS     0x00005000   // E820 entries (24 bytes each)
#define E820_BOOT_COUNT_PHYS    0x00004FF0   // Entry count (uint16_t)
#define E820_BOOT_MAX_ENTRIES   64

// =================================================================
// SECTION 8: PAE CONSTANTS
// =================================================================

// PAE model window: virtual address where layers get mapped
#define PAE_MODEL_WINDOW_VIRT   0xE9200000   // Follows Ring 2 (at 0xE1010000 + 128MB = 0xE9010000)
#define PAE_MODEL_WINDOW_SIZE   0x08000000   // 128MB (Reduced from 256MB to fit below KV window if needed, or keep 256MB if space allows)

// PAE KV cache window
#define PAE_KV_WINDOW_VIRT      0xF1200000   // Follows Model Window
#define PAE_KV_WINDOW_SIZE      0x04000000   // 64MB

// Ensure PAE windows don't overlap with existing regions
STATIC_ASSERT(PAE_MODEL_WINDOW_VIRT >= RING2_VIRT_END,
    "FATAL: PAE model window overlaps Ring 2 territory!");
STATIC_ASSERT((PAE_MODEL_WINDOW_VIRT + PAE_MODEL_WINDOW_SIZE) <= PAE_KV_WINDOW_VIRT,
    "FATAL: PAE model window overlaps KV window!");
STATIC_ASSERT((PAE_KV_WINDOW_VIRT + PAE_KV_WINDOW_SIZE) <= 0xFD000000,
    "FATAL: PAE KV window overlaps framebuffer!");

#endif // MEMORY_MAP_H
