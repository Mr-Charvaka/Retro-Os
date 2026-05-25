#include "e820.h"
#include "pmm.h"
#include "../drivers/serial.h"
#include "../include/string.h"
#include "../include/memory_map.h"

MemoryMap g_memory_map;

#define E820_TYPE_USABLE        1
#define E820_TYPE_RESERVED      2
#define E820_TYPE_ACPI_RECLAIM  3
#define E820_TYPE_ACPI_NVS      4
#define E820_TYPE_BAD_MEMORY    5

static const char* e820_type_name(uint32_t type) {
    switch (type) {
        case E820_TYPE_USABLE:        return "Usable";
        case E820_TYPE_RESERVED:      return "Reserved";
        case E820_TYPE_ACPI_RECLAIM:  return "ACPI Reclaimable";
        case E820_TYPE_ACPI_NVS:      return "ACPI NVS";
        case E820_TYPE_BAD_MEMORY:    return "Bad Memory";
        default:                      return "Unknown";
    }
}

void e820_parse(bool paging_active) {
    // ===== ADD THIS: Zero the entire struct before populating =====
    memset(&g_memory_map, 0, sizeof(g_memory_map));
    // ==============================================================

    uint32_t count_addr = paging_active ? (E820_BOOT_COUNT_PHYS + KERNEL_VIRT_OFFSET) : E820_BOOT_COUNT_PHYS;
    uint32_t data_addr  = paging_active ? (E820_BOOT_DATA_PHYS + KERNEL_VIRT_OFFSET) : E820_BOOT_DATA_PHYS;
    
    uint16_t raw_count = *(volatile uint16_t*)count_addr;
    serial_log_hex("E820: Raw entry count: ", (uint32_t)raw_count);
    
    if (raw_count == 0) {
        g_memory_map.e820_valid = false;
        g_memory_map.regions[0].base = 0;
        g_memory_map.regions[0].length = TOTAL_RAM;
        g_memory_map.regions[0].end = TOTAL_RAM;
        g_memory_map.regions[0].type = E820_TYPE_USABLE;
        g_memory_map.region_count = 1;
        g_memory_map.total_memory = TOTAL_RAM;
        g_memory_map.usable_below_4gb = TOTAL_RAM;
        return;
    }
    
    E820Entry* raw_entries = (E820Entry*)data_addr;
    uint32_t valid_count = 0;
    for (uint16_t i = 0; i < raw_count && valid_count < E820_BOOT_MAX_ENTRIES; i++) {
        E820Entry* raw = &raw_entries[i];
        if (raw->length == 0) continue;
        
        MemoryRegion* region = &g_memory_map.regions[valid_count];
        region->base = raw->base;
        region->length = raw->length;
        region->end = raw->base + raw->length;
        region->type = raw->type;
        region->above_4gb = (region->end > (uint64_t)0x100000000ULL);
        
        if (raw->type == E820_TYPE_USABLE) {
            g_memory_map.total_memory += raw->length;
            if (region->end > g_memory_map.highest_address) g_memory_map.highest_address = region->end;
            
            if (raw->base >= 0x100000000ULL) {
                g_memory_map.usable_above_4gb += raw->length;
                g_memory_map.has_high_memory = true;
                if (g_memory_map.high_mem_base == 0) g_memory_map.high_mem_base = raw->base;
            } else if (region->end > 0x100000000ULL) {
                uint64_t below = 0x100000000ULL - raw->base;
                uint64_t above = region->end - 0x100000000ULL;
                g_memory_map.usable_below_4gb += below;
                g_memory_map.usable_above_4gb += above;
                g_memory_map.has_high_memory = true;
                if (g_memory_map.high_mem_base == 0) g_memory_map.high_mem_base = 0x100000000ULL;
            } else {
                g_memory_map.usable_below_4gb += raw->length;
            }
        }
        valid_count++;
    }
    g_memory_map.region_count = valid_count;
    g_memory_map.e820_valid = true;
    g_memory_map.high_mem_size = g_memory_map.usable_above_4gb;
    g_memory_map.high_mem_2mb_chunks = (uint32_t)(g_memory_map.high_mem_size / (2ULL * 1024 * 1024));
}

void e820_print_map() {
    serial_log("E820 Memory Map:");
    for (uint32_t i = 0; i < g_memory_map.region_count; i++) {
        MemoryRegion* r = &g_memory_map.regions[i];
        serial_log_hex("  Base Hi: ", (uint32_t)(r->base >> 32));
        serial_log_hex("  Base Lo: ", (uint32_t)r->base);
        serial_log_hex("  End Hi:  ", (uint32_t)(r->end >> 32));
        serial_log_hex("  End Lo:  ", (uint32_t)r->end);
        serial_log(e820_type_name(r->type));
    }
    serial_log_hex("Usable High Memory MB: ", (uint32_t)(g_memory_map.usable_above_4gb / (1024*1024)));
}

uint32_t e820_get_usable_low_memory() {
    // Return the HIGHEST usable address below 4GB
    // (not the total bytes — PMM needs the address range to bitmap)
    uint32_t highest = 0;
    for (uint32_t i = 0; i < g_memory_map.region_count; i++) {
        MemoryRegion* r = &g_memory_map.regions[i];
        if (r->type != E820_TYPE_USABLE) continue;
        if (r->base >= 0x100000000ULL) continue;
        
        uint64_t end = r->end;
        if (end > 0x100000000ULL) end = 0x100000000ULL;
        if ((uint32_t)end > highest) highest = (uint32_t)end;
    }
    return highest;
}

uint64_t e820_get_usable_high_memory() { return g_memory_map.usable_above_4gb; }

void e820_init_pmm_regions() {
    if (!g_memory_map.e820_valid) {
        pmm_mark_region_free(0x0, TOTAL_RAM);
        return;
    }
    for (uint32_t i = 0; i < g_memory_map.region_count; i++) {
        MemoryRegion* r = &g_memory_map.regions[i];
        if (r->type != 1 || r->base >= 0x100000000ULL) continue;
        uint64_t rend = r->end > 0x100000000ULL ? 0x100000000ULL : r->end;
        pmm_mark_region_free((uint32_t)r->base, (uint32_t)(rend - r->base));
    }
}
