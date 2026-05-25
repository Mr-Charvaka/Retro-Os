#include "pae.h"
#include "pmm.h"
#include "apic.h"
#include "../drivers/serial.h"
#include <stdint.h>
#include "process.h" // For spinlock_lock/unlock
volatile int paging_lock = 0;

// External Hardware/ISR Protos
extern "C" {
    void hpet_map_hardware();
    void apic_map_hardware();
    void pae_register_handler(void);
}

// Low-level Assembly/Feat Protos
extern "C" uint32_t cpuid_edx_feat(void);
extern "C" uint32_t cpuid_ext_edx_feat(void);
extern "C" void pae_invlpg_asm(uint32_t addr);
extern "C" void pae_flush_tlb_asm(void);
extern "C" void pae_enable(uint32_t pdpt_phys, bool nx_enabled);

// ============================================================
// GLOBAL PAE STATE
// ============================================================

extern "C" {
    pae_pdpt_t           g_pdpt __attribute__((aligned(32)));
    pae_page_directory_t g_page_dirs[4] __attribute__((aligned(4096)));
    pae_page_table_t     g_kernel_pts[KERNEL_PT_COUNT] __attribute__((aligned(4096)));
    pae_page_table_t     g_sliding_pt __attribute__((aligned(4096)));
    uint32_t g_pdpt_phys_addr = 0;
    bool g_pae_active = false;
    extern uint32_t *kernel_directory; // Legacy pointer for compatibility
}

// ============================================================
// CORE: CPU FEATURE DETECTION
// ============================================================

bool pae_check_cpu_support(void) {
    uint32_t edx = cpuid_edx_feat();
    bool has_pae = (edx & (1 << 6)) != 0;
    serial_log(has_pae ? "PAE: CPU supports PAE." : "PAE: Internal Error - CPU lacks PAE.");
    return has_pae;
}

bool pae_check_nx_support(void) {
    uint32_t edx = cpuid_ext_edx_feat();
    bool has_nx = (edx & (1 << 20)) != 0;
    serial_log(has_nx ? "PAE: NX support detected." : "PAE: NX bit NOT supported (Expected).");
    return has_nx;
}

// ============================================================
// CORE: AUDIT
// ============================================================

void pae_verify_no_nx(void) {
    // Audit PDPT
    for (int i = 0; i < 4; i++) g_pdpt.entries[i] &= ~PAE_RESERVED_MASK;
    // Audit PDs
    for (int d = 0; d < 4; d++) {
        for (int i = 0; i < 512; i++) g_page_dirs[d].entries[i] &= ~PAE_RESERVED_MASK;
    }
    // Audit PTs
    for (int t = 0; t < KERNEL_PT_COUNT; t++) {
        for (int i = 0; i < 512; i++) g_kernel_pts[t].entries[i] &= ~PAE_RESERVED_MASK;
    }
    serial_log("PAE: Safety audit - zeroed potential NX bits.");
}

// ============================================================
// STRUCTURE INITIALIZATION
// ============================================================

void pae_init(void) {
    serial_log("\n[PAE] Initializing 3-level Paging (NX-Free)...");

    if (!pae_check_cpu_support()) return;

    // Reset everything
    for(int i=0; i<4; i++) g_pdpt.entries[i] = 0;
    for(int i=0; i<4; i++) {
        for(int j=0; j<512; j++) g_page_dirs[i].entries[j] = 0;
    }
    for(int i=0; i<KERNEL_PT_COUNT; i++) {
        for(int j=0; j<512; j++) g_kernel_pts[i].entries[j] = 0;
    }
    for(int i=0; i<512; i++) g_sliding_pt.entries[i] = 0;

    // 1. Wire PDPT to Page Directories
    for (int i = 0; i < 4; i++) {
        uint32_t pd_phys = (uint32_t)(uintptr_t)&g_page_dirs[i] - KERNEL_VIRT_OFFSET;
        g_pdpt.entries[i] = (uint64_t)pd_phys | PAE_FLAG_PRESENT;
    }
    g_pdpt_phys_addr = (uint32_t)(uintptr_t)&g_pdpt - KERNEL_VIRT_OFFSET;

    // 2. Identity Map PDPT[0] (0-1GB) for Kernel Transition and PDPT[2] (2-3GB) for MMIO
    // We leave PDPT[1] (1-2GB) empty for User space program allocation (INIT.ELF).
    
    // --- IDENTITY MAPPING (0 - 1GB) ---
    // Identity map PDPT[0] (0-1GB) to allow the kernel 
    // to transition during early boot and secondary cores to access low-memory.
    // map 0-1GB as identity (first 256 entries in PD 0)
    for (int i = 0; i < 512; i++) {
        uint64_t phys = (uint64_t)i * 0x200000ULL;
        // Identity map PDPT[0] (0-1GB) to allow AP trampoline transition
        g_page_dirs[0].entries[i] = phys | PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE | PAE_FLAG_PAGESIZE;
    }

    // --- USER SPACE / DYNAMIC REGION (1GB - 3GB) ---
    // We leave PDPT[1] and PDPT[2] unmapped (all entries zero).
    // This allows the ELF loader and PMM to map these on-demand.
    for (int d = 1; d <= 2; d++) {
        for (int i = 0; i < 512; i++) g_page_dirs[d].entries[i] = 0;
    }

    // --- HIGHER-HALF KERNEL & MMIO (3.0GB - 4.0GB) ---
    // Virtual 3.0GB to 3.75GB maps to Physical 0 to 0.75GB
    // Virtual 3.75GB to 4.00GB maps to Physical 3.75GB to 4.00GB (Hardware MMIO)
    for (int i = 0; i < 512; i++) {
        uint32_t virt_off = (uint32_t)i * 0x200000;
        uint32_t virt_addr = 0xC0000000 + virt_off;
        uint64_t phys;
        uint64_t flags = PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE | PAE_FLAG_PAGESIZE | PAE_FLAG_GLOBAL;

        if (virt_addr >= 0xF8000000) {
            // MMIO Window: Identity mapping for PCI/BGA/APIC
            phys = (uint64_t)virt_addr;
            flags |= PAE_FLAG_PCD | PAE_FLAG_PWT; // Cache Disable for hardware
        } else {
            // Kernel RAM Window: Maps back to Physical 0+
            phys = (uint64_t)virt_off;
        }

        g_page_dirs[3].entries[i] = phys | flags;
    }

    // --- 4KB HYBRID OVERLAY (Kernel Stack/Text) ---
    // Override the first 128MB of PDPT[3] with high-performance 4KB Page Tables.
    for (int i = 0; i < KERNEL_PT_COUNT; i++) {
        uint32_t pt_phys = (uint32_t)(uintptr_t)&g_kernel_pts[i] - KERNEL_VIRT_OFFSET;
        g_page_dirs[3].entries[i] = (uint64_t)pt_phys | PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE | PAE_FLAG_GLOBAL;
    }

    // Populate the 4KB entries in g_kernel_pts (3.0GB to 3.128GB)
    for (uint32_t p = 0; p < (KERNEL_PT_COUNT * 512); p++) {
        uint32_t virt = 0xC0000000 + (p * 4096);
        uint64_t phys = (uint64_t)(p * 4096);
        uint32_t pd_idx = PAE_PD_INDEX(virt);
        uint32_t pt_idx = PAE_PT_INDEX(virt);
        if (pd_idx < KERNEL_PT_COUNT) {
            g_kernel_pts[pd_idx].entries[pt_idx] = (phys & PAE_ADDR_MASK) | PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE | PAE_FLAG_GLOBAL;
        }
    }

    // 4. Identity Map High MMIO (0xE0000000 - 0xFFFFFFFF) in PDPT[3]
    // Removed: Conflicts with 4KB paging_map calls in AHCI/BGA drivers.
    // Drivers will use paging_map() to create proper 4KB tables on demand.

    // 4. Sliding Window (Last 2MB Entry - Index 511)
    uint32_t slide_pt_phys = (uint32_t)(uintptr_t)&g_sliding_pt - KERNEL_VIRT_OFFSET;
    g_page_dirs[3].entries[511] = (uint64_t)slide_pt_phys | PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE;

    // 5. Audit & Fault Handler
    pae_verify_no_nx();
    pae_register_handler();

    serial_log("PAE: Ready for switch.");
    
    // Perform Switch
    bool has_nx = pae_check_nx_support();
    pae_enable(g_pdpt_phys_addr, has_nx); // Pass physical address explicitly
    g_pae_active = true;

    // IMPORTANT: Re-map hardware after PAE is enabled.
    // Legacy mapping in init_paging() was lost because it only modified legacy tables.
    apic_map_hardware();
    hpet_map_hardware();

    serial_log("[PAE] Enabled successfully.");
}

// ============================================================
// INTERNAL UTILITIES (Lock-free variants)
// ============================================================

void* pae_map_window_unlocked(uint64_t phys_addr_64, int slot) {
    if (slot < 0 || slot >= PAE_SLIDING_WINDOW_SLOTS) return nullptr;
    uint32_t virt = PAE_SLIDING_WINDOW_BASE + (slot * 4096);
    uint64_t page_phys = (phys_addr_64 & PAE_ADDR_MASK);
    g_sliding_pt.entries[slot] = page_phys | PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE;
    pae_invlpg(virt);
    return (void*)(virt + (uint32_t)(phys_addr_64 & 0xFFF));
}

void pae_unmap_window_unlocked(int slot) {
    if (slot < 0 || slot >= PAE_SLIDING_WINDOW_SLOTS) return;
    uint32_t virt = PAE_SLIDING_WINDOW_BASE + (slot * 4096);
    g_sliding_pt.entries[slot] = 0;
    pae_invlpg(virt);
}

// ============================================================
// PAE-SAFE INTERFACES FOR VM.H / PAGING.H
// ============================================================

// Helper to get or create a 4KB Page Table for a virtual address.
// Returns the PHYSICAL address of the page table.
static uint32_t pae_get_or_create_pt(uint32_t virt) {
    uint32_t pdpt_idx = PAE_PDPT_INDEX(virt);
    uint32_t pd_idx = PAE_PD_INDEX(virt);

    // Get current PDPT from CR3
    uint32_t pdpt_phys;
    asm volatile("mov %%cr3, %0" : "=r"(pdpt_phys));
    
    // Map the PDPT temporarily to read/update the PDPT entry (Slot 2)
    pae_pdpt_t *pdpt = (pae_pdpt_t *)pae_map_window_unlocked((uint64_t)pdpt_phys, 2);
    
    // Check if the Page Directory exists for this PDPT entry
    if (!(pdpt->entries[pdpt_idx] & PAE_FLAG_PRESENT)) {
        uint32_t new_pd_phys = (uint32_t)(uintptr_t)pmm_alloc_block();
        if (!new_pd_phys) {
            pae_unmap_window_unlocked(2);
            return 0;
        }

        // Zero the new PD (Slot 1)
        uint64_t *pd_temp = (uint64_t *)pae_map_window_unlocked((uint64_t)new_pd_phys, 1);
        for (int i = 0; i < 512; i++) pd_temp[i] = 0;
        pae_unmap_window_unlocked(1);

        pdpt->entries[pdpt_idx] = (uint64_t)new_pd_phys | PAE_FLAG_PRESENT;
        
        // CRITICAL: PDPT change requires CR3 reload (full TLB flush)
        pae_flush_tlb();
        if (total_cpus > 1) smp_send_ipi_all_but_self(IPI_VECTOR_TLB_SHOOTDOWN);
    }

    uint32_t pd_phys = (uint32_t)(pdpt->entries[pdpt_idx] & PAE_ADDR_MASK);
    pae_unmap_window_unlocked(2);

    // Map the Page Directory temporarily to read/update the PDE (Slot 1)
    pae_page_directory_t *pd = (pae_page_directory_t *)pae_map_window_unlocked((uint64_t)pd_phys, 1);
    uint64_t *pde = &pd->entries[pd_idx];

    if (!(*pde & PAE_FLAG_PRESENT) || (*pde & PAE_FLAG_PAGESIZE)) {
        uint32_t pt_phys = (uint32_t)(uintptr_t)pmm_alloc_block();
        if (!pt_phys) {
            pae_unmap_window_unlocked(1);
            return 0;
        }

        // Zero the new PT (Slot 0)
        uint64_t *pt_temp = (uint64_t *)pae_map_window_unlocked((uint64_t)pt_phys, 0);
        for (int i = 0; i < 512; i++) pt_temp[i] = 0;
        pae_unmap_window_unlocked(0);

        *pde = (uint64_t)pt_phys | PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE | PAE_FLAG_USER;
        smp_tlb_shootdown(virt);
    }

    uint32_t pte_phys_base = (uint32_t)(*pde & PAE_ADDR_MASK);
    pae_unmap_window_unlocked(1);
    return pte_phys_base;
}

extern "C" void pae_map_page(uint32_t virt, uint64_t phys, uint64_t flags) {
    uint32_t pdpt_idx = PAE_PDPT_INDEX(virt);
    uint32_t pd_idx = PAE_PD_INDEX(virt);
    uint32_t pt_idx = PAE_PT_INDEX(virt);

    // Use spinlock for multi-core safety
    spinlock_lock(&paging_lock);

    if (pdpt_idx == 3) {
        g_kernel_pts[pd_idx].entries[pt_idx] = (phys & PAE_ADDR_MASK) | flags | PAE_FLAG_PRESENT;
        smp_tlb_shootdown(virt);
        spinlock_unlock(&paging_lock);
        return;
    }

    uint32_t pt_phys = pae_get_or_create_pt(virt);
    if (pt_phys) {
        // Map the PT safely using the window
        uint64_t *pt_window = (uint64_t *)pae_map_window_unlocked((uint64_t)pt_phys, 0);
        pt_window[pt_idx] = (phys & PAE_ADDR_MASK) | flags | PAE_FLAG_PRESENT;
        pae_unmap_window_unlocked(0);
        smp_tlb_shootdown(virt);
    }

    spinlock_unlock(&paging_lock);
}

extern "C" void pae_map_range(uint32_t virt, uint64_t phys, uint32_t count, uint64_t flags) {
    spinlock_lock(&paging_lock);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t cur_virt = virt + (i * 4096);
        uint64_t cur_phys = phys + (i * 4096);
        uint32_t pdpt_idx = PAE_PDPT_INDEX(cur_virt);
        uint32_t pd_idx = PAE_PD_INDEX(cur_virt);
        uint32_t pt_idx = PAE_PT_INDEX(cur_virt);

        if (pdpt_idx == 3) {
            g_kernel_pts[pd_idx].entries[pt_idx] = (cur_phys & PAE_ADDR_MASK) | flags | PAE_FLAG_PRESENT;
            continue;
        }

        uint32_t pt_phys = pae_get_or_create_pt(cur_virt);
        if (pt_phys) {
            uint64_t *pt_window = (uint64_t *)pae_map_window_unlocked((uint64_t)pt_phys, 0);
            pt_window[pt_idx] = (cur_phys & PAE_ADDR_MASK) | flags | PAE_FLAG_PRESENT;
            pae_unmap_window_unlocked(0);
        }
    }
    
    // One single shootdown for the entire range (Full TLB flush if range is large)
    if (count > 32) {
        pae_flush_tlb();
        if (total_cpus > 1) smp_send_ipi_all_but_self(IPI_VECTOR_TLB_SHOOTDOWN);
    } else {
        for(uint32_t i=0; i<count; i++) smp_tlb_shootdown(virt + (i*4096));
    }

    spinlock_unlock(&paging_lock);
}

extern "C" void pae_unmap_page(uint32_t virt) {
    uint32_t pdpt_idx = PAE_PDPT_INDEX(virt);
    uint32_t pd_idx = PAE_PD_INDEX(virt);
    uint32_t pt_idx = PAE_PT_INDEX(virt);

    spinlock_lock(&paging_lock);
    if (pdpt_idx == 3) {
        g_kernel_pts[pd_idx].entries[pt_idx] = 0;
        smp_tlb_shootdown(virt);
    }
    spinlock_unlock(&paging_lock);
}

extern "C" uint64_t vm_get_phys_pae(uint32_t virt) {
    uint32_t pdpt_idx = PAE_PDPT_INDEX(virt);
    uint32_t pd_idx = PAE_PD_INDEX(virt);
    uint32_t pt_idx = PAE_PT_INDEX(virt);

    if (!(g_page_dirs[pdpt_idx].entries[pd_idx] & PAE_FLAG_PRESENT)) return 0;
    
    // If it's a 2MB page
    if (g_page_dirs[pdpt_idx].entries[pd_idx] & PAE_FLAG_PAGESIZE) {
        return (g_page_dirs[pdpt_idx].entries[pd_idx] & 0xFFE00000ULL) | (virt & 0x1FFFFFULL);
    }

    // Need to look into PT (only for PDPT[3] in our current setup)
    if (pdpt_idx == 3) {
        if (!(g_kernel_pts[pd_idx].entries[pt_idx] & PAE_FLAG_PRESENT)) return 0;
        return (g_kernel_pts[pd_idx].entries[pt_idx] & PAE_ADDR_MASK) | (virt & 0xFFFULL);
    }
    
    return 0;
}

// ============================================================
// UTILITIES
// ============================================================

void* pae_map_window(uint64_t phys_addr_64, int slot) {
    spinlock_lock(&paging_lock);
    return pae_map_window_unlocked(phys_addr_64, slot);
}

void pae_unmap_window(int slot) {
    pae_unmap_window_unlocked(slot);
    spinlock_unlock(&paging_lock);
}

void pae_invlpg(uint32_t virtual_addr) {
    pae_invlpg_asm(virtual_addr);
}

void pae_flush_tlb(void) {
    pae_flush_tlb_asm();
}
