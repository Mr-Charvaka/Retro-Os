#include "pae.h"
#include "pmm.h"
#include "../drivers/serial.h"
#include "../include/memory_map.h"
#include "../include/isr.h"
#include "process.h"

// Page fault error bits
#define PF_PRESENT   (1 << 0)
#define PF_WRITE     (1 << 1)
#define PF_USER      (1 << 2)
#define PF_RESERVED  (1 << 3)
#define PF_INSTFETCH (1 << 4)

static void dump_regs(registers_t *regs) {
    serial_log("\n--- REGISTER DUMP ---");
    serial_log_hex("  EAX: ", regs->eax);
    serial_log_hex("  EBX: ", regs->ebx);
    serial_log_hex("  ECX: ", regs->ecx);
    serial_log_hex("  EDX: ", regs->edx);
    serial_log_hex("  ESI: ", regs->esi);
    serial_log_hex("  EDI: ", regs->edi);
    serial_log_hex("  EBP: ", regs->ebp);
    serial_log_hex("  ESP: ", regs->esp);
    serial_log_hex("  EIP: ", regs->eip);
    serial_log_hex("  CS : ", regs->cs);
    serial_log_hex("  DS : ", regs->ds);
    serial_log_hex("  EFLAGS: ", regs->eflags);
    if (regs->cs == 0x1B) {
        serial_log_hex("  USER ESP: ", regs->useresp);
        serial_log_hex("  SS : ", regs->ss);
    }
    serial_log("---------------------\n");
}

extern bool handle_demand_paging(uint32_t faulting_address);

extern "C" void pae_page_fault_handler(registers_t *regs) {
    uint32_t fault_addr;
    uint32_t error_code = regs->err_code;
    asm volatile("mov %%cr2, %0" : "=r"(fault_addr));

    // === TRY GENERAL DEMAND PAGING FIRST ===
    // This handles: user heap, user stack, kernel heap, SHM, 
    // and privilege-bit upgrades for PAE pages.
    if (!(error_code & PF_PRESENT)) {
        if (handle_demand_paging(fault_addr)) {
            return;  // Page mapped, instruction will retry
        }
    }

    // === DEMAND PAGING FOR RING 2 (AI BRAIN) ===
    // If a Ring 2 process accesses its extended territory (Weights, Model Window,
    // or KV Cache), we map physical frames on-demand.
    if (!(error_code & PF_USER) && (fault_addr >= 0xD2000000 && fault_addr < 0xFD000000)) {
        if (!(error_code & PF_PRESENT)) {
            void *phys = pmm_alloc_block();
            if (phys) {
                // Map as Ring 2 Data (Present, Writable, User=1 for R2/R3)
                pae_map_page(fault_addr & 0xFFFFF000, (uint64_t)(uintptr_t)phys, PAE_RING2_DATA);
                // Instruction will be retried automatically
                return;
            } else {
                serial_log("[PAE-PF] FATAL: OUT OF PHYSICAL BLOCKS FOR RING 2!\n");
            }
        }
    }

    serial_log("\n[PAE-PF] Page Fault Detected!");
    serial_log_hex("  Fault Address (CR2): ", fault_addr);
    serial_log_hex("  Error Code:          ", error_code);
    dump_regs(regs);

    // 1. Reserved bit violation
    if (error_code & PF_RESERVED) {
        serial_log("!!! PAE RESERVED BIT FAULT !!!");
        serial_log("Potential NX bit set on non-NX CPU?");
        while(1) { asm volatile("cli; hlt"); }
    }

    // 2. Ring 2 Region Guard Checks
    if (fault_addr >= (RING2_STACK_VIRT - 4096) && fault_addr < RING2_STACK_VIRT) {
        serial_log("[PAE-PF] Ring 2 STACK UNDERFLOW!");
        while(1) { asm volatile("cli; hlt"); }
    }
    uint32_t stack_top = RING2_STACK_VIRT + RING2_STACK_SIZE;
    if (fault_addr >= stack_top && fault_addr < (stack_top + 4096)) {
        serial_log("[PAE-PF] Ring 2 STACK OVERFLOW!");
        while(1) { asm volatile("cli; hlt"); }
    }

    // 3. Ring 2 Write Violations
    if ((error_code & PF_WRITE) && (error_code & PF_PRESENT)) {
        if (fault_addr >= RING2_CODE_VIRT && fault_addr < (RING2_CODE_VIRT + RING2_CODE_SIZE)) {
            serial_log("[PAE-PF] Attempt to modify Ring 2 CODE blocked.");
            while(1) { asm volatile("cli; hlt"); }
        }
        if (fault_addr >= RING2_MODEL_VIRT && fault_addr < (RING2_MODEL_VIRT + RING2_MODEL_SIZE)) {
            serial_log("[PAE-PF] Attempt to modify Ring 2 WEIGHTS blocked.");
            while(1) { asm volatile("cli; hlt"); }
        }
    }

    // 4. Default: User Mode vs Kernel Mode
    if (error_code & PF_USER) {
        serial_log("PAE: User process crashed (Segmentation Fault). Terminating...\n");
        exit_process(-11); // SIGSEGV like exit code
        // exit_process calls schedule(), so it won't return
    }

    serial_log("PAE: Halted on Unhandled KERNEL Fault.");
    while(1) { asm volatile("cli; hlt"); }
}

extern "C" void pae_register_handler(void) {
    register_interrupt_handler(14, (isr_t)pae_page_fault_handler);
    serial_log("PAE: Page fault handler registered at INT 14.");
}
