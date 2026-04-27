#ifndef PMM_HIGH_H
#define PMM_HIGH_H

#include "../include/types.h"

#define PMM_HIGH_CHUNK_SIZE     (2 * 1024 * 1024)
#define PMM_HIGH_CHUNK_SHIFT    21
#define PMM_HIGH_MAX_CHUNKS     30720
#define PMM_HIGH_BITMAP_WORDS   ((PMM_HIGH_MAX_CHUNKS + 31) / 32)

struct PMMHighState {
    uint32_t bitmap[PMM_HIGH_BITMAP_WORDS];
    uint64_t base_address;
    uint64_t total_size;
    uint32_t total_chunks;
    uint32_t used_chunks;
    bool     initialized;
};

extern PMMHighState g_pmm_high;

void pmm_high_init();
uint64_t pmm_high_alloc(uint32_t num_chunks);
void pmm_high_free(uint64_t phys_addr, uint32_t num_chunks);
uint32_t pmm_high_get_free_chunks();
void pmm_high_print_stats();

#endif
