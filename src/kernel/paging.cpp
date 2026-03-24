#include "paging.h"
#include "../drivers/hpet.h"
#include "../drivers/serial.h"
#include "../include/signal.h"
#include "../include/string.h"
#include "../kernel/memory.h"
#include "apic.h"
#include "pmm.h"
#include "process.h"

uint32_t *kernel_directory = 0;
uint32_t *current_directory = 0;

extern "C" uint32_t _text_start;
extern "C" uint32_t _text_end;
extern "C" uint32_t _rodata_start;
extern "C" uint32_t _rodata_end;

#include "shm.h"
#include "vm.h"

// Circular include se bachne ke liye jugad
typedef struct process process_t;
extern process_t *current_process;

void page_fault_handler(registers_t *regs);
bool handle_demand_paging(uint32_t faulting_address);

void page_fault_handler(registers_t *regs) {
  uint32_t faulting_address;
  asm volatile("mov %%cr2, %0" : "=r"(faulting_address));

  if (handle_demand_paging(faulting_address)) {
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

  // ── Already-mapped page with wrong privilege bits ──────────────────────
  uint32_t *pte = paging_get_pte(addr);
  if (pte && (*pte & 1)) {
    if (!(*pte & 4)) { // Page is Supervisor-only
      // Only upgrade to User if it is genuinely inside the process's
      // allowed address space — prevents privilege escalation.
      bool in_heap  = current_process &&
                      addr >= 0x00400000 &&
                      addr  < (current_process->heap_end + HEAP_GRACE_BYTES) &&
                      addr  < 0x70000000;
      bool in_stack = current_process &&
                      addr >= (current_process->user_stack_top - 0x1000) &&
                      addr  < current_process->user_stack_top;
      if (in_heap || in_stack) {
        uint32_t page_base = addr & 0xFFFFF000;
        uint32_t phys = (uint32_t)pmm_alloc_block();
        if (!phys) {
          serial_log("DEMAND: OOM upgrading supervisor page to user");
          return false;
        }
        serial_log_hex("DEMAND: Upgrading supervisor page to user at ", addr);
        vm_map_page(phys, page_base, 7); // User | RW | Present
        return true;
      }
    }
    return false; // Wrong privilege and outside allowed regions → SIGSEGV
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
      uint32_t phys_base = (uint32_t)(uintptr_t)seg->phys_addr +
                           (page_base - seg->virt_start);
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
    uint32_t stack_top    = current_process->user_stack_top;
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
}

void init_paging() {
  serial_log("PAGING: Memory map taiyar kar rahe hain...");

  uint32_t phys_pd = (uint32_t)pmm_alloc_block();
  kernel_directory = (uint32_t *)PHYS_TO_VIRT(phys_pd);
  memset(kernel_directory, 0, 4096);

  // Map 1 GB of physical memory (0-1GB mapped to 3GB-4GB)
  for (int j = 0; j < 256; j++) {
    uint32_t phys_pt = (uint32_t)pmm_alloc_block();
    uint32_t *virt_pt = (uint32_t *)PHYS_TO_VIRT(phys_pt);
    memset(virt_pt, 0, 4096);

    for (int i = 0; i < 1024; i++) {
      uint32_t phys_addr = j * 4 * 1024 * 1024 + i * 4096;
      uint32_t virt_addr = phys_addr + KERNEL_VIRTUAL_BASE;

      // Allow Write access to the first 1GB identity map to support library initialization
      uint32_t flags = 3; 
      virt_pt[i] = phys_addr | flags;
    }

    // 1. Higher-Half Mapping (3GB se upar)
    kernel_directory[KERNEL_PAGE_DIRECTORY_INDEX + j] = phys_pt | 3;

    // 2. Identity Mapping (0-512MB) - ACPI aur legacy drivers ke liye zaroori
    // start mein
    kernel_directory[j] = phys_pt | 3;
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
