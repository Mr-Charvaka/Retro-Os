#ifndef E820_H
#define E820_H

#include "../include/types.h"
#include "../include/memory_map.h"

// Raw E820 entry as stored by bootloader (24 bytes)
struct E820Entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_ext;
} __attribute__((packed));

// Parsed memory region
struct MemoryRegion {
    uint64_t base;
    uint64_t length;
    uint64_t end;
    uint32_t type;
    bool     above_4gb;
};

struct MemoryMap {
    MemoryRegion regions[E820_BOOT_MAX_ENTRIES];
    uint32_t     region_count;
    uint64_t total_memory;
    uint64_t usable_below_4gb;
    uint64_t usable_above_4gb;
    uint64_t highest_address;
    uint64_t high_mem_base;
    uint64_t high_mem_size;
    uint32_t high_mem_2mb_chunks;
    bool     e820_valid;
    bool     has_high_memory;
};

extern MemoryMap g_memory_map;

void e820_parse(bool paging_active);
void e820_print_map();
uint32_t e820_get_usable_low_memory();
uint64_t e820_get_usable_high_memory();
bool e820_is_usable(uint64_t base, uint64_t size);
void e820_init_pmm_regions();

#endif
