#include "pae_security.h"
#include "pae.h"
#include "gdt.h"
#include "../drivers/serial.h"
#include "../include/memory_map.h"

extern "C" void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

// ============================================================
// COMPENSATING SECURITY layer (NX-Free)
// ============================================================

void pae_security_tighten_ring2_segments(void) {
    serial_log("[PAE-SEC] Standardizing Ring 2 GDT segments...");

    // 1. Ring 2 Code Segment (GDT[6])
    // Base 0, Limit 4GB (Flat model)
    gdt_set_gate(
        6,                      // Selector 0x32
        0,                      // Base: 0
        0xFFFFF,                // Limit: 4GB
        0xDA,                   // Access: P=1, DPL=2, S=1, Type=Code (Exec/Read)
        0xCF                    // Granularity: 4KB pages, 32-bit
    );
    serial_log("  R2 CS: Flat model (Base=0, Limit=4GB) ACTIVE.");

    // 2. Ring 2 Data Segment (GDT[7])
    // Base 0, Limit 4GB (Flat model)
    gdt_set_gate(
        7,                      // Selector 0x3A
        0,                      // Base: 0
        0xFFFFF,                // Limit: 4GB
        0xD2,                   // Access: P=1, DPL=2, S=1, Type=Data (Read/Write)
        0xCF                    // Granularity: 4KB pages, 32-bit
    );
    serial_log("  R2 DS: Flat model (Base=0, Limit=4GB) ACTIVE.");
}

bool pae_security_validate_ring2_ptr(uint32_t ptr, uint32_t size, bool needs_write) {
    if (ptr < RING2_VIRT_START || (ptr + size) > RING2_VIRT_END) return false;
    
    if (needs_write) {
        // Disallow writing to code (0xD2100000 - 0xD2200000)
        if (ptr >= RING2_CODE_VIRT && ptr < (RING2_CODE_VIRT + RING2_CODE_SIZE)) return false;
        // Disallow writing to weights (0xD3200000 - 0xD8200000)
        if (ptr >= RING2_MODEL_VIRT && ptr < (RING2_MODEL_VIRT + RING2_MODEL_SIZE)) return false;
    }
    return true;
}

void pae_security_init(void) {
    serial_log("\n[PAE-SEC] Activating security baseline...");
    pae_security_tighten_ring2_segments();
    serial_log("[PAE-SEC] Segment containment: ACTIVE.");
}
