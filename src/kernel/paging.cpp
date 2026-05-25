#include "paging.h"
#include "../drivers/hpet.h"
#include "../drivers/serial.h"
#include "../include/signal.h"
#include "../include/string.h"
#include "../kernel/memory.h"
#include "apic.h"
#include "pae.h"
#include "pmm.h"
#include "process.h"

extern "C" {
extern pae_page_directory_t g_page_dirs[4];
extern pae_page_table_t g_kernel_pts[KERNEL_PT_COUNT];
uint32_t *kernel_directory = 0;
}

uint32_t *current_directory = 0;

extern "C" uint32_t _text_start;
extern "C" uint32_t _text_end;
extern "C" uint32_t _rodata_start;
extern "C" uint32_t _rodata_end;

#include "shm.h"
#include "vm.h"

// Circular include se bachne ke liye jugad
typedef struct process process_t;
// current_process macro from process.h will be used

extern "C" bool g_pae_active;
extern "C" void pae_map_page(uint32_t virt, uint64_t phys, uint64_t flags);
extern "C" uint64_t *paging_get_pte_pae(uint32_t virt);

void page_fault_handler(registers_t *regs);
bool handle_demand_paging(uint32_t faulting_address);

void page_fault_handler(registers_t *regs) {
  uint32_t faulting_address;
  asm volatile("mov %%cr2, %0" : "=r"(faulting_address));

  if (handle_demand_paging(faulting_address)) {
    smp_tlb_shootdown(faulting_address);
    return; // Galti sudhar li!
  }

  serial_log("PAGE FAULT! Address:");
  serial_log_hex("", faulting_address);
  serial_log_hex("  EIP: ", regs->eip);
  serial_log_hex("  Error Code: ", regs->err_code);

  int us = regs->err_code & 0x4;
  if (us) {
    serial_log("PAGE FAULT: Killing user process.");
    sys_kill(current_process->id, SIGSEGV);
    return;
  }

  serial_log("KERNEL PANIC: Page Fault ho gaya");
  for (;;)
    ;
}

// ============================================================================
// handle_demand_paging - Flagship hardened version
//
// FIX #1 — "The Paging Border Hole"
//
// Original flaw: ANY address in 0x00400000..0x70000000 was unconditionally
// mapped as user-heap.  An app could touch addresses far beyond its declared
// heap_end and receive fresh physical frames — effectively nibbling up to the
// SHM region and the kernel's internal stacks.
//
// Fix: The user-heap check now enforces:
//   addr < current_process->heap_end + HEAP_GRACE_BYTES
// The "grace" (2 MB) covers the common pattern where malloc() probes slightly
// past the current brk before calling brk/sbrk to extend it.  Accesses beyond
// that are genuine out-of-bounds and are refused, causing a SIGSEGV.
//
// Stack region is similarly capped to the process's declared user_stack_top.
// ============================================================================

// Grace window beyond heap_end that demand-paging still accepts.
// 2 MB is enough for any reasonable malloc growth probe.
#define HEAP_GRACE_BYTES (2u * 1024u * 1024u)

bool handle_demand_paging(uint32_t addr) {

  // ── Existing privilege upgrade stays the same ──
  if (g_pae_active) {
    uint32_t pdpt_idx = PAE_PDPT_INDEX(addr);
    uint32_t pd_idx = PAE_PD_INDEX(addr);
    uint32_t pt_idx = PAE_PT_INDEX(addr);

    uint32_t pdpt_phys;
    asm volatile("mov %%cr3, %0" : "=r"(pdpt_phys));

    pae_pdpt_t *pdpt = (pae_pdpt_t *)pae_map_window((uint64_t)pdpt_phys, 2);
    if (!(pdpt->entries[pdpt_idx] & 1)) {
      pae_unmap_window(2);
      return false;
    }
    uint32_t pd_phys = (uint32_t)(pdpt->entries[pdpt_idx] & PAE_ADDR_MASK);
    pae_unmap_window(2);

    pae_page_directory_t *pd =
        (pae_page_directory_t *)pae_map_window((uint64_t)pd_phys, 1);
    if (!(pd->entries[pd_idx] & 1) ||
        (pd->entries[pd_idx] & PAE_FLAG_PAGESIZE)) {
      pae_unmap_window(1);
      return false;
    }
    uint32_t pt_phys = (uint32_t)(pd->entries[pd_idx] & PAE_ADDR_MASK);
    pae_unmap_window(1);

    uint64_t *pt = (uint64_t *)pae_map_window((uint64_t)pt_phys, 0);
    if (pt[pt_idx] & 1) {
      if (!(pt[pt_idx] & 4)) {
        bool in_heap = current_process && addr >= 0x00400000 &&
                       addr < (current_process->heap_end + HEAP_GRACE_BYTES);
        if (in_heap) {
          pt[pt_idx] |= 4; // Add USER bit
          pae_unmap_window(0);
          return true;
        }
      }
    }
    pae_unmap_window(0);
  } else {
    uint32_t *pte = paging_get_pte(addr);
    if (pte && (*pte & 1)) {
      if (!(*pte & 4)) {
        bool in_heap = current_process && addr >= 0x00400000 &&
                       addr < (current_process->heap_end + HEAP_GRACE_BYTES);
        if (in_heap) {
          *pte |= 4;
          return true;
        }
      }
    }
  }

  // ── Ring 2 guard zone hit — give clear diagnostic, do NOT map ─────────
  // This catches accesses to:
  //   - Stack underflow guard (0xD1FF0000 - 0xD2000000)
  //   - Stack overflow guard  (0xD2010000 - 0xD2100000)
  //   - End guard             (0xD9200000 - 0xDA000000)
  // These MUST remain unmapped. Mapping them would defeat their purpose.
  if (addr >= RING2_STACK_UNDERFLOW_GUARD_VIRT && addr < RING2_VIRT_END) {
    // Check if it's specifically a guard zone
    bool in_underflow_guard = (addr < RING2_STACK_VIRT);
    bool in_overflow_guard =
        (addr >= RING2_STACK_GUARD_VIRT && addr < RING2_CODE_VIRT);
    bool in_end_guard = (addr >= RING2_END_GUARD_VIRT);

    if (in_underflow_guard) {
      serial_log("RING 2 FAULT: Stack underflow! ESP went below stack bottom.");
      serial_log_hex("  Faulting address: ", addr);
      serial_log_hex("  Stack bottom:     ", RING2_STACK_VIRT);
      return false;
    }
    if (in_overflow_guard) {
      serial_log("RING 2 FAULT: Stack overflow guard hit!");
      serial_log_hex("  Faulting address: ", addr);
      return false;
    }
    if (in_end_guard) {
      serial_log("RING 2 FAULT: Access beyond Ring 2 territory!");
      serial_log_hex("  Faulting address: ", addr);
      return false;
    }

    // Address is within valid Ring 2 mapped region but page isn't present.
    // This shouldn't happen since init_paging maps all of Ring 2.
    serial_log("RING 2 FAULT: Unexpected unmapped page in Ring 2 territory!");
    serial_log_hex("  Faulting address: ", addr);
    return false;
  }

  // ── 0. Kernel Heap demand paging (above pre-mapped 512 MB window) ──────
  if (addr >= 0xE0000000) {
    uint32_t page_base = addr & 0xFFFFF000;
    uint32_t phys = (uint32_t)pmm_alloc_block();
    if (!phys) {
      serial_log("DEMAND: OOM in kernel heap demand paging");
      return false;
    }
    vm_map_page(phys, page_base, 3); // Supervisor | RW | Present
    return true;
  }

  if (!current_process)
    return false;

  // ── 1. Shared Memory (0x70000000 – 0x80000000) ────────────────────────
  if (addr >= 0x70000000 && addr < 0x80000000) {
    shm_segment_t *seg = shm_get_segment(addr);
    if (seg) {
      uint32_t page_base = addr & 0xFFFFF000;
      uint32_t phys_base =
          (uint32_t)(uintptr_t)seg->phys_addr + (page_base - seg->virt_start);
      serial_log_hex("DEMAND: Mapping SHM at ", addr);
      vm_map_page(phys_base, page_base, 7);
      return true;
    }
    serial_log_hex("DEMAND FAIL: SHM segment not found for ", addr);
    return false;
  }

  // ── 2. User Heap (0x00400000 – heap_end + grace) — HARDENED ───────────
  //
  // CRITICAL: we now reject addresses beyond heap_end + HEAP_GRACE_BYTES.
  // This prevents an app from silently mapping memory right up to the
  // SHM region or the kernel stack guard pages.
  if (addr >= 0x00400000 && addr < 0x70000000) {
    uint32_t heap_limit = current_process->heap_end + HEAP_GRACE_BYTES;
    if (heap_limit < current_process->heap_end) // overflow guard
      heap_limit = 0x70000000;
    heap_limit = (heap_limit < 0x70000000) ? heap_limit : 0x70000000;

    if (addr < heap_limit) {
      uint32_t page_base = addr & 0xFFFFF000;
      uint32_t phys = (uint32_t)pmm_alloc_block();
      if (!phys) {
        serial_log("DEMAND: OOM in user heap");
        return false;
      }
      serial_log_hex("DEMAND: Mapping user heap at ", addr);
      vm_map_page(phys, page_base, 7);
      return true;
    }
    // addr is beyond heap_end + grace → out-of-bounds, deliver SIGSEGV
    serial_log_hex("DEMAND REJECT: Access beyond heap_end at ", addr);
    serial_log_hex("  heap_end = ", current_process->heap_end);
    return false;
  }

  // ── 3. User Stack (grows downward from user_stack_top) — HARDENED ──────
  //
  // Allow the stack to grow down by at most 4 MB from the declared top.
  // A deeper fault is a stack overflow → SIGSEGV.
  {
    uint32_t stack_top = current_process->user_stack_top;
    uint32_t stack_bottom = (stack_top >= 0x400000) ? stack_top - 0x400000 : 0;
    if (addr >= stack_bottom && addr < stack_top) {
      uint32_t page_base = addr & 0xFFFFF000;
      uint32_t phys = (uint32_t)pmm_alloc_block();
      if (!phys) {
        serial_log("DEMAND: OOM in user stack");
        return false;
      }
      serial_log_hex("DEMAND: Mapping user stack page at ", addr);
      vm_map_page(phys, page_base, 7);
      return true;
    }
  }

  return false; // Unknown/forbidden region → page fault escalates to SIGSEGV
}

void paging_map(uint32_t phys, uint32_t virt, uint32_t flags) {
  if (g_pae_active) {
    pae_map_page(virt, (uint64_t)phys, (uint64_t)flags);
    return;
  }
  uint32_t pd_index = virt >> 22;
  uint32_t pt_index = (virt >> 12) & 0x03FF;

  if (!(kernel_directory[pd_index] & 1)) {
    uint32_t phys_pt = (uint32_t)pmm_alloc_block();
    uint32_t *virt_pt = (uint32_t *)PHYS_TO_VIRT(phys_pt);
    memset(virt_pt, 0, 4096);
    kernel_directory[pd_index] = phys_pt | 7;
  }

  uint32_t *pt =
      (uint32_t *)PHYS_TO_VIRT(kernel_directory[pd_index] & 0xFFFFF000);
  pt[pt_index] = (phys & 0xFFFFF000) | flags;
  smp_tlb_shootdown(virt);
}

void init_paging() {
  serial_log("PAGING: Memory map taiyar kar rahe hain...");

  uint32_t phys_pd = (uint32_t)pmm_alloc_block();
  kernel_directory = (uint32_t *)PHYS_TO_VIRT(phys_pd);
  memset(kernel_directory, 0, 4096);

  // Map 512MB
  for (int j = 0; j < 128; j++) {
    uint32_t phys_pt = (uint32_t)pmm_alloc_block();
    uint32_t *virt_pt = (uint32_t *)PHYS_TO_VIRT(phys_pt);
    memset(virt_pt, 0, 4096);

    for (int i = 0; i < 1024; i++) {
      uint32_t phys_addr = j * 4 * 1024 * 1024 + i * 4096;
      uint32_t virt_addr = phys_addr + KERNEL_VIRTUAL_BASE;

      uint32_t flags = 3;
      if (virt_addr >= (uint32_t)&_text_start &&
          virt_addr < (uint32_t)&_text_end) {
        flags = 1;
      } else if (virt_addr >= (uint32_t)&_rodata_start &&
                 virt_addr < (uint32_t)&_rodata_end) {
        flags = 1;
      } else if (virt_addr >= RING2_CODE_VIRT &&
                 virt_addr < (RING2_CODE_VIRT + RING2_CODE_SIZE)) {
        flags = 1; // Ring 2 Code - Read-only
      } else if (virt_addr >= RING2_MODEL_VIRT &&
                 virt_addr < (RING2_MODEL_VIRT + RING2_MODEL_SIZE)) {
        flags = 1; // Ring 2 Model - Read-only
      } else if (virt_addr >= RING2_STACK_GUARD_VIRT &&
                 virt_addr <
                     (RING2_STACK_GUARD_VIRT + RING2_STACK_GUARD_SIZE)) {
        flags = 0; // Ring 2 Stack Guard - UNMAPPED
      } else if (virt_addr >= RING2_END_GUARD_VIRT &&
                 virt_addr < (RING2_END_GUARD_VIRT + RING2_END_GUARD_SIZE)) {
        flags = 0; // Ring 2 End Guard - UNMAPPED
      } else if (virt_addr >= RING2_STACK_UNDERFLOW_GUARD_VIRT &&
                 virt_addr < (RING2_STACK_UNDERFLOW_GUARD_VIRT +
                              RING2_STACK_UNDERFLOW_GUARD_SIZE)) {
        flags = 0; // Ring 2 Stack Underflow Guard - UNMAPPED
                   // Catches stack-grows-downward overflow
      }

      if (flags != 0) {
        virt_pt[i] = phys_addr | flags;
      } else {
        virt_pt[i] = 0; // Explicitly unmapped
      }
    }

    // 1. Higher-Half Mapping (3GB se upar)
    kernel_directory[KERNEL_PAGE_DIRECTORY_INDEX + j] = phys_pt | 3;

    // 2. Identity Mapping (0-512MB) - ACPI aur legacy drivers ke liye zaroori
    // start mein
    kernel_directory[j] = phys_pt | 3;
  }

  // Rev AC: Pre-Map the Full Ring 2 AI Territory (128 MB)
  // This ensures zero-latency allocation for weights, activations, and buffers.
  serial_log("PAGING: Pre-Mapping Full AI Territory (128 MB)...");
  uint32_t total_pages = RING2_REGION_SIZE / 4096;
  uint32_t virt_base = RING2_VIRT_START;
  
  for (uint32_t i = 0; i < total_pages; i++) {
    uint32_t virt = virt_base + (i * 4096);
    uint32_t phys = (uint32_t)pmm_alloc_block();
    if (!phys) break; 

    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x03FF;

    if (!(kernel_directory[pd_index] & 1)) {
        uint32_t pt_p = (uint32_t)pmm_alloc_block();
        uint32_t *pt_v = (uint32_t *)PHYS_TO_VIRT(pt_p);
        memset(pt_v, 0, 4096);
        kernel_directory[pd_index] = pt_p | 7;
    }
    uint32_t *pt = (uint32_t *)PHYS_TO_VIRT(kernel_directory[pd_index] & 0xFFFFF000);
    pt[pt_index] = phys | 7; 
  }

  register_interrupt_handler(14, page_fault_handler);

  // Note: apic/hpet map paging_map use karte hain jo ab pointers sahi handle
  // karta hai
  apic_map_hardware();
  hpet_map_hardware();

  switch_page_directory(kernel_directory);
  serial_log("PAGING: Higher-Half & Identity Enabled.");
}

void switch_page_directory(uint32_t *dir) {
  current_directory = dir;
  uint32_t phys = VIRT_TO_PHYS(dir);
  asm volatile("mov %0, %%cr3" ::"r"(phys));
  uint32_t cr0;
  asm volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 |= 0x80010000;
  asm volatile("mov %0, %%cr0" ::"r"(cr0));
}

// Virtual address se PTE nikalne ka jugad
uint32_t *paging_get_pte(uint32_t virt) {
  uint32_t pd_index = virt >> 22;
  uint32_t pt_index = (virt >> 12) & 0x03FF;

  // Kaunsa directory use karna hai dekho
  uint32_t *dir = current_directory ? current_directory : kernel_directory;
  if (!dir)
    return 0;

  // Check karo page table hai ya nahi
  if (!(dir[pd_index] & 1))
    return 0;

  // Page table uthao
  uint32_t *pt = (uint32_t *)PHYS_TO_VIRT(dir[pd_index] & 0xFFFFF000);
  return &pt[pt_index];
}

// ============================================================================
// paging_get_pte_pae - Implementation for 3-level PAE
// ============================================================================
extern "C" uint64_t paging_get_phys_generic(uint32_t virt) {
  if (!g_pae_active) {
    uint32_t phys_pd;
    asm volatile("mov %%cr3, %0" : "=r"(phys_pd));
    uint32_t *pd = (uint32_t *)PHYS_TO_VIRT(phys_pd);
    uint32_t pd_index = virt >> 22;
    if (!(pd[pd_index] & 1)) {
      if (virt >= KERNEL_VIRTUAL_BASE)
        return (uint64_t)(virt - KERNEL_VIRTUAL_BASE);
      return (uint64_t)virt;
    }
    uint32_t *pt = (uint32_t *)PHYS_TO_VIRT(pd[pd_index] & 0xFFFFF000);
    if (!(pt[(virt >> 12) & 0x3FF] & 1)) {
      if (virt >= KERNEL_VIRTUAL_BASE)
        return (uint64_t)(virt - KERNEL_VIRTUAL_BASE);
      return (uint64_t)virt;
    }
    return (uint64_t)(pt[(virt >> 12) & 0x3FF] & 0xFFFFF000) | (virt & 0xFFF);
  }

  uint32_t pdpt_idx = PAE_PDPT_INDEX(virt);
  uint32_t pd_idx = PAE_PD_INDEX(virt);
  uint32_t pt_idx = PAE_PT_INDEX(virt);

  uint32_t pdpt_phys;
  asm volatile("mov %%cr3, %0" : "=r"(pdpt_phys));

  // Safety check: Don't resolve null
  if (virt < 0x1000)
    return 0;

  // Use sliding window for traversal
  pae_pdpt_t *pdpt = (pae_pdpt_t *)pae_map_window((uint64_t)pdpt_phys, 2);
  if (!(pdpt->entries[pdpt_idx] & 1)) {
    pae_unmap_window(2);
    if (virt >= 0xC0000000)
      return (uint64_t)(virt - 0xC0000000);
    return 0;
  }
  uint32_t pd_phys = (uint32_t)(pdpt->entries[pdpt_idx] & PAE_ADDR_MASK);
  pae_unmap_window(2);

  pae_page_directory_t *pd =
      (pae_page_directory_t *)pae_map_window((uint64_t)pd_phys, 1);
  if (!(pd->entries[pd_idx] & 1)) {
    pae_unmap_window(1);
    if (virt >= 0xC0000000)
      return (uint64_t)(virt - 0xC0000000);
    return 0;
  }

  if (pd->entries[pd_idx] & PAE_FLAG_PAGESIZE) {
    uint64_t phys_base = pd->entries[pd_idx] & 0x0000000FFFE00000ULL;
    pae_unmap_window(1);
    return (uint64_t)(phys_base | (virt & 0x1FFFFF));
  }

  uint32_t pt_phys = (uint32_t)(pd->entries[pd_idx] & PAE_ADDR_MASK);
  pae_unmap_window(1);

  uint64_t *pt = (uint64_t *)pae_map_window((uint64_t)pt_phys, 0);
  if (!(pt[pt_idx] & 1)) {
    pae_unmap_window(0);
    if (virt >= 0xC0000000)
      return (uint64_t)(virt - 0xC0000000);
    return 0;
  }

  uint64_t phys_final = (pt[pt_idx] & PAE_ADDR_MASK) | (virt & 0xFFF);
  pae_unmap_window(0);

  // Log important resolutions (e.g. user-space or kernel-base boundary)
  if (virt == 0x40000000 || virt == 0xC0000000) {
    serial_log_hex("PAE-PHYS-RESOLVE: ", (uint32_t)(phys_final & 0xFFFFFFFF));
  }

  return phys_final;
}

extern "C" void* pd_get_virt_addr(uintptr_t pd_phys, uint32_t vaddr) {
    if (!g_pae_active) {
        uint32_t *pd = (uint32_t *)PHYS_TO_VIRT(pd_phys);
        uint32_t pt_p = pd[vaddr >> 22];
        if (!(pt_p & 1)) return nullptr;
        uint32_t *pt = (uint32_t *)PHYS_TO_VIRT(pt_p & 0xFFFFF000);
        uint32_t pte = pt[(vaddr >> 12) & 0x3FF];
        if (!(pte & 1)) return nullptr;
        return (void *)PHYS_TO_VIRT((pte & 0xFFFFF000) | (vaddr & 0xFFF));
    } else {
        // PAE resolution
        uint64_t phys = paging_get_phys_generic(vaddr);
        if (!phys) return nullptr;
        return (void *)PHYS_TO_VIRT((uint32_t)phys);
    }
}
