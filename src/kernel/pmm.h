#ifndef PMM_H
#define PMM_H

#include "../include/types.h"

// 4 KB block/frame size
#define PMM_BLOCK_SIZE    4096
#define PMM_BLOCKS_PER_BYTE 8

/**
 * pmm_init - Initialise the PMM.
 * After this call ALL frames are reserved.
 * The caller must follow up with pmm_mark_region_free() for each
 * BIOS/UEFI "available RAM" region, then pmm_mark_region_used() for
 * the kernel image, MMIO holes, and the bitmap buffer itself.
 */
void pmm_init(uint32_t mem_size, uint32_t *bitmap);

/** Allocate one 4 KB physical frame. Returns physical address or 0 on OOM. */
void *pmm_alloc_block();

/** Allocate N contiguous 4 KB physical frames. Returns base address or 0. */
void *pmm_alloc_contiguous_blocks(uint32_t count);

/** Free N contiguous frames allocated with pmm_alloc_contiguous_blocks(). */
void pmm_free_contiguous_blocks(void *p, uint32_t count);

/** Free a single 4 KB frame. Double-free safe — logs and ignores repeated frees. */
void pmm_free_block(void *p);

/**
 * pmm_mark_region_used - Reserve a physical address range.
 * Call for: kernel code/data, MMIO windows, ACPI tables, the PMM bitmap.
 */
void pmm_mark_region_used(uint32_t base, uint32_t size);

/**
 * pmm_mark_region_free - Release a BIOS-declared available region.
 * Frame 0 is never freed regardless of arguments.
 */
void pmm_mark_region_free(uint32_t base, uint32_t size);

/**
 * pmm_validate_region - Assert that every frame in [base, base+size) is
 * currently allocated (in-use).  Returns true on success, false if any
 * frame in the range is still free (indicating a setup error).
 */
bool pmm_validate_region(uint32_t base, uint32_t size);

/** Number of free 4 KB frames. */
uint32_t pmm_get_free_block_count();

/** Total frames managed by the PMM. */
uint32_t pmm_get_block_count();

/** Print memory statistics to serial. */
void pmm_print_stats();

#endif // PMM_H
