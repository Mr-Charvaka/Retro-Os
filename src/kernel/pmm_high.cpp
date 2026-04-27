#include "pmm_high.h"
#include "e820.h"
#include "../drivers/serial.h"
#include "../include/string.h"

PMMHighState g_pmm_high;

static inline void high_set(uint32_t chunk) { g_pmm_high.bitmap[chunk / 32] |= (1u << (chunk % 32)); }
static inline void high_clear(uint32_t chunk) { g_pmm_high.bitmap[chunk / 32] &= ~(1u << (chunk % 32)); }
static inline bool high_test(uint32_t chunk) { return (g_pmm_high.bitmap[chunk / 32] & (1u << (chunk % 32))) != 0; }

void pmm_high_init() {
    serial_log("PMM_HIGH: Initializing...");
    memset(&g_pmm_high, 0, sizeof(PMMHighState));
    memset(g_pmm_high.bitmap, 0xFF, sizeof(g_pmm_high.bitmap));
    
    if (!g_memory_map.has_high_memory) {
        g_pmm_high.initialized = true;
        return;
    }
    
    g_pmm_high.base_address = g_memory_map.high_mem_base;
    g_pmm_high.total_size = g_memory_map.high_mem_size;
    g_pmm_high.total_chunks = g_memory_map.high_mem_2mb_chunks;
    g_pmm_high.used_chunks = g_pmm_high.total_chunks;
    
    if (g_pmm_high.total_chunks > PMM_HIGH_MAX_CHUNKS) g_pmm_high.total_chunks = PMM_HIGH_MAX_CHUNKS;
    
    for (uint32_t i = 0; i < g_memory_map.region_count; i++) {
        MemoryRegion* r = &g_memory_map.regions[i];
        if (r->type != 1 || r->end <= 0x100000000ULL) continue;
        uint64_t rstart = r->base < 0x100000000ULL ? 0x100000000ULL : r->base;
        uint64_t astart = (rstart + PMM_HIGH_CHUNK_SIZE - 1) & ~((uint64_t)PMM_HIGH_CHUNK_SIZE - 1);
        uint64_t aend = r->end & ~((uint64_t)PMM_HIGH_CHUNK_SIZE - 1);
        if (astart >= aend) continue;
        
        uint32_t cstart = (uint32_t)((astart - g_pmm_high.base_address) >> PMM_HIGH_CHUNK_SHIFT);
        uint32_t cend = (uint32_t)((aend - g_pmm_high.base_address) >> PMM_HIGH_CHUNK_SHIFT);
        if (cend > g_pmm_high.total_chunks) cend = g_pmm_high.total_chunks;
        
        for (uint32_t c = cstart; c < cend; c++) {
            if (high_test(c)) { high_clear(c); g_pmm_high.used_chunks--; }
        }
    }
    g_pmm_high.initialized = true;
    pmm_high_print_stats();
}

uint64_t pmm_high_alloc(uint32_t num_chunks) {
    if (!g_pmm_high.initialized || num_chunks == 0) return 0;
    uint32_t consecutive = 0;
    uint32_t start = 0;
    for (uint32_t i = 0; i < g_pmm_high.total_chunks; i++) {
        if (!high_test(i)) {
            if (consecutive == 0) start = i;
            if (++consecutive >= num_chunks) {
                for (uint32_t j = start; j < start + num_chunks; j++) high_set(j);
                g_pmm_high.used_chunks += num_chunks;
                return g_pmm_high.base_address + ((uint64_t)start << PMM_HIGH_CHUNK_SHIFT);
            }
        } else consecutive = 0;
    }
    return 0;
}

void pmm_high_free(uint64_t phys_addr, uint32_t num_chunks) {
    if (!g_pmm_high.initialized || phys_addr < g_pmm_high.base_address) return;
    uint32_t start = (uint32_t)((phys_addr - g_pmm_high.base_address) >> PMM_HIGH_CHUNK_SHIFT);
    for (uint32_t i = 0; i < num_chunks; i++) {
        uint32_t c = start + i;
        if (c < g_pmm_high.total_chunks && high_test(c)) { high_clear(c); g_pmm_high.used_chunks--; }
    }
}

uint32_t pmm_high_get_free_chunks() { return g_pmm_high.total_chunks - g_pmm_high.used_chunks; }

void pmm_high_print_stats() {
    serial_log("High Memory Stats:");
    serial_log_hex("  Total Chunks: ", g_pmm_high.total_chunks);
    serial_log_hex("  Used Chunks:  ", g_pmm_high.used_chunks);
    serial_log_hex("  Free Chunks:  ", pmm_high_get_free_chunks());
}
