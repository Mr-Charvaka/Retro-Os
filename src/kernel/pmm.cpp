// pmm.cpp - Flagship Physical Memory Manager
//
// FIX #5 — "The Zero-Check PMM"
//
// Original flaw: pmm_init() blindly clearred the entire bitmap to 0 (free),
// meaning it would happily hand out frames that sit inside:
//   • BIOS / ACPI reserved regions
//   • Memory holes (non-existent RAM on the physical bus)
//   • Hardware-memory-mapped I/O windows
//   • The kernel image itself (if not separately marked used)
// Any of those handed to a normal allocation caused an instant, un-debuggable
// Kernel Panic.
//
// Fix strategy — layered defence:
//   1. pmm_init() now marks EVERYTHING as used (safe default).
//   2. The caller (boot code) explicitly marks regions the BIOS map declares
//      as "available RAM" as free with pmm_mark_region_free().
//   3. pmm_mark_region_used() is called for the kernel image, the bitmap
//      itself, the first megabyte (reserved), and any MMIO holes.
//   4. pmm_alloc_block() now validates the returned frame is (a) inside the
//      declared physical address space, (b) non-zero (frame 0 is sacred:
//      it holds the IVT on real hardware), and (c) actually marked free in
//      the bitmap before marking it used — catching any bitmap corruption.
//   5. A new pmm_validate_region() helper lets callers sanity-check a
//      contiguous range before use (e.g., DMA buffer allocation).

#include "pmm.h"
#include "../drivers/serial.h"
#include "../include/string.h"

/* ── Internal constants ────────────────────────────────────────────────── */
#define BLOCKS_PER_UINT32  32
#define FRAME_ZERO_GUARD   1  // Never allocate physical frame 0 (IVT / BIOS)

/* ── Bitmap state ──────────────────────────────────────────────────────── */
static uint32_t *pmm_bitmap    = 0;
static uint32_t  pmm_mem_size  = 0;  // Total physical bytes this PMM manages
static uint32_t  pmm_max_blocks = 0; // Total 4 KB frames
static uint32_t  pmm_used_blocks = 0;

/* ── Bit helpers (bit=1 → frame in use, bit=0 → frame free) ──────────── */
static inline void mmap_set(int bit) {
    pmm_bitmap[bit / 32] |=  (1u << (bit % 32));
}
static inline void mmap_unset(int bit) {
    pmm_bitmap[bit / 32] &= ~(1u << (bit % 32));
}
static inline int mmap_test(int bit) {
    return (int)(pmm_bitmap[bit / 32] & (1u << (bit % 32)));
}

/* ── First-free scan ───────────────────────────────────────────────────── */
static int mmap_first_free() {
    for (uint32_t i = 0; i < pmm_max_blocks / 32; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) { // At least one free bit
            for (int j = 0; j < 32; j++) {
                if (!(pmm_bitmap[i] & (1u << j))) {
                    int frame = (int)(i * 32 + j);
                    // Guard: never give out frame 0 (IVT / real-mode BIOS)
                    if (frame == 0)
                        continue;
                    return frame;
                }
            }
        }
    }
    return -1; // OOM
}

/* ═══════════════════════════════════════════════════════════════════════ */
/*  PUBLIC API                                                             */
/* ═══════════════════════════════════════════════════════════════════════ */

/**
 * pmm_init - Initialise the physical memory manager.
 *
 * @mem_size : Total physical RAM in bytes (from BIOS/UEFI memory map).
 * @bitmap   : Pointer to a pre-allocated kernel buffer for the bitmap.
 *             Must be at least ceil(mem_size / (4096*8)) bytes long.
 *
 * IMPORTANT: After calling pmm_init() everything is USED.
 * The caller MUST call pmm_mark_region_free() for every BIOS-declared
 * "available" region, then pmm_mark_region_used() for the kernel image,
 * MMIO windows, and the bitmap itself.
 */
void pmm_init(uint32_t mem_size, uint32_t *bitmap) {
    pmm_mem_size   = mem_size;
    pmm_bitmap     = bitmap;
    pmm_max_blocks = mem_size / PMM_BLOCK_SIZE;

    // FIX: Start with EVERYTHING marked used (safe default).
    // Only regions the BIOS explicitly declares available will be freed.
    uint32_t bitmap_bytes = (pmm_max_blocks + 7) / 8;
    memset((uint8_t *)pmm_bitmap, 0xFF, bitmap_bytes); // 0xFF = all used
    pmm_used_blocks = pmm_max_blocks;

    serial_log("PMM: Initialised (all frames reserved - awaiting BIOS map).");
    serial_log_hex("  Physical mem size : ", mem_size);
    serial_log_hex("  Total frames      : ", pmm_max_blocks);
    serial_log_hex("  Bitmap at         : ", (uint32_t)(uintptr_t)bitmap);
}

/**
 * pmm_mark_region_used - Reserve a physical address range.
 * Rounds base DOWN to block boundary, size UP to next block.
 */
void pmm_mark_region_used(uint32_t base, uint32_t size) {
    if (size == 0) return;
    uint32_t align  = base / PMM_BLOCK_SIZE;
    uint32_t blocks = (size + PMM_BLOCK_SIZE - 1) / PMM_BLOCK_SIZE;

    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t frame = align + b;
        if (frame >= pmm_max_blocks) break; // Past end of managed memory
        if (!mmap_test(frame)) {
            mmap_set(frame);
            pmm_used_blocks++;
        }
    }
}

/**
 * pmm_mark_region_free - Release a BIOS-declared available physical region.
 * Frame 0 is never freed (IVT guard).
 */
void pmm_mark_region_free(uint32_t base, uint32_t size) {
    if (size == 0) return;
    uint32_t align  = base / PMM_BLOCK_SIZE;
    uint32_t blocks = (size + PMM_BLOCK_SIZE - 1) / PMM_BLOCK_SIZE;

    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t frame = align + b;
        if (frame == 0) continue;          // Never free the IVT frame
        if (frame >= pmm_max_blocks) break;
        if (mmap_test(frame)) {
            mmap_unset(frame);
            pmm_used_blocks--;
        }
    }
}

/**
 * pmm_alloc_block - Allocate one 4 KB physical frame.
 *
 * Hardened checks:
 *   • OOM pre-check
 *   • Bitmap scan for first free frame (skips frame 0)
 *   • Double-validates the found frame is actually free (bitmap corruption guard)
 *   • Validates returned address is < pmm_mem_size
 *
 * Returns physical address, or 0 on failure.
 */
void *pmm_alloc_block() {
    static uint32_t alloc_count = 0;

    if (pmm_get_free_block_count() == 0) {
        serial_log("PMM: OOM - no free frames.");
        serial_log_hex("  Used  : ", pmm_used_blocks);
        serial_log_hex("  Total : ", pmm_max_blocks);
        return 0;
    }

    int frame = mmap_first_free();
    if (frame < 0) {
        serial_log("PMM: OOM (bitmap inconsistency detected).");
        serial_log_hex("  used_blocks counter : ", pmm_used_blocks);
        serial_log_hex("  max_blocks          : ", pmm_max_blocks);
        return 0;
    }

    // Sanity guard: the frame must be genuinely free before we take it
    if (mmap_test(frame)) {
        serial_log_hex("PMM: CORRUPTION — mmap_first_free returned a used frame: ",
                       (uint32_t)frame);
        return 0;
    }

    // Sanity guard: frame must be within managed physical memory
    uint32_t addr = (uint32_t)frame * PMM_BLOCK_SIZE;
    if (addr >= pmm_mem_size) {
        serial_log_hex("PMM: CORRUPTION — frame beyond mem_size: ", addr);
        return 0;
    }

    mmap_set(frame);
    pmm_used_blocks++;
    alloc_count++;

    return (void *)addr;
}

/**
 * pmm_alloc_contiguous_blocks - Allocate N contiguous 4 KB physical frames.
 * Skips frame 0.  Returns physical base address or 0.
 */
void *pmm_alloc_contiguous_blocks(uint32_t count) {
    if (count == 0) return 0;
    if (pmm_get_free_block_count() < count) return 0;

    uint32_t consecutive = 0;
    int      start_frame = -1;

    for (uint32_t i = 1; i < pmm_max_blocks; i++) { // start at 1: skip frame 0
        if (!mmap_test(i)) {
            if (consecutive == 0)
                start_frame = (int)i;
            consecutive++;
            if (consecutive == count) {
                for (uint32_t k = 0; k < count; k++)
                    mmap_set(start_frame + k);
                pmm_used_blocks += count;
                return (void *)(uint32_t)((uint32_t)start_frame * PMM_BLOCK_SIZE);
            }
        } else {
            consecutive  = 0;
            start_frame  = -1;
        }
    }
    serial_log_hex("PMM: No contiguous block of frames: ", count);
    return 0;
}

/**
 * pmm_free_block - Release a single physical frame back to the pool.
 * Guards against double-free and freeing frame 0.
 */
void pmm_free_block(void *p) {
    uint32_t addr  = (uint32_t)p;
    uint32_t frame = addr / PMM_BLOCK_SIZE;

    if (frame == 0) {
        serial_log("PMM: Attempted free of frame 0 - blocked.");
        return;
    }
    if (frame >= pmm_max_blocks) {
        serial_log_hex("PMM: Free of out-of-range frame: ", addr);
        return;
    }
    if (!mmap_test(frame)) {
        serial_log_hex("PMM: Double-free detected at: ", addr);
        return; // Double-free guard
    }
    mmap_unset(frame);
    pmm_used_blocks--;
}

/**
 * pmm_free_contiguous_blocks - Release N contiguous frames.
 */
void pmm_free_contiguous_blocks(void *p, uint32_t count) {
    uint32_t addr  = (uint32_t)p;
    uint32_t frame = addr / PMM_BLOCK_SIZE;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t f = frame + i;
        if (f == 0 || f >= pmm_max_blocks) continue;
        if (mmap_test(f)) {
            mmap_unset(f);
            pmm_used_blocks--;
        } else {
            serial_log_hex("PMM: Double-free in contiguous range at frame: ", f);
        }
    }
}

/**
 * pmm_validate_region - Check that every frame in [base, base+size) is
 * currently allocated (in-use).  Useful for DMA/MMIO setup code to assert
 * a region is properly reserved before handing it to hardware.
 * Returns true if all frames are marked used, false if any are free.
 */
bool pmm_validate_region(uint32_t base, uint32_t size) {
    uint32_t frame  = base / PMM_BLOCK_SIZE;
    uint32_t blocks = (size + PMM_BLOCK_SIZE - 1) / PMM_BLOCK_SIZE;
    for (uint32_t i = 0; i < blocks; i++) {
        if (frame + i >= pmm_max_blocks) return false;
        if (!mmap_test(frame + i)) {
            serial_log_hex("PMM: validate_region found FREE frame: ", frame + i);
            return false;
        }
    }
    return true;
}

/* ── Stats ─────────────────────────────────────────────────────────────── */

uint32_t pmm_get_free_block_count() {
    return pmm_max_blocks - pmm_used_blocks;
}

uint32_t pmm_get_block_count() {
    return pmm_max_blocks;
}

void pmm_print_stats() {
    uint32_t free_blocks = pmm_get_free_block_count();
    serial_log("PMM Stats:");
    serial_log_hex("  Total frames  : ", pmm_max_blocks);
    serial_log_hex("  Used frames   : ", pmm_used_blocks);
    serial_log_hex("  Free frames   : ", free_blocks);
    serial_log_hex("  Free RAM (KB) : ", free_blocks * 4);
    serial_log_hex("  Used RAM (KB) : ", pmm_used_blocks * 4);
}
