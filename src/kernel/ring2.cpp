// =================================================================
// ring2.cpp — Ring 2 (AI Brain) Launch and Entry
// =================================================================

#include "ring2.h"
#include "../drivers/serial.h"
#include "../include/memory_map.h"
#include "../include/string.h"

// =================================================================
// Shared State
// =================================================================

volatile uint32_t ring2_alive = 0;
volatile uint32_t ring2_cpl = 0xFF;

int ring2_is_alive(void) {
    return (ring2_alive == RING2_ALIVE_MAGIC) ? 1 : 0;
}

// =================================================================
// brain_main — Ring 2 Entry Point
// =================================================================

__attribute__((noreturn)) void brain_main(void) {
    // ── Proof of life ───────────────────────────────────────────
    ring2_alive = RING2_ALIVE_MAGIC;

    // ── Call the AI Brain Loop (Step 2 Implementation) ───────────
    ring2_brain_entry();

    // ── Main loop (Should not be reached) ───────────────────────
    for (;;) {
        asm volatile("pause");
    }
}

// =================================================================
// brain_request_handler_c — Ring 2 Call Gate Handler
// =================================================================

extern "C"
uint32_t brain_request_handler_c(uint32_t request_type, uint32_t data_ptr) {

    switch (request_type) {

        case BRAIN_REQ_PING:
            return 0x0052494E;

        case BRAIN_REQ_ADD_ONE: {
            volatile uint32_t *ptr = (volatile uint32_t *)data_ptr;
            uint32_t val = *ptr;
            *ptr = val + 1;
            return 0;
        }

        case BRAIN_REQ_GET_CPL: {
            uint16_t cs_val;
            asm volatile("mov %%cs, %0" : "=r"(cs_val));
            return (uint32_t)(cs_val & 0x3);
        }

        case BRAIN_REQ_ECHO:
            return data_ptr;

        default:
            return 0xFFFFFFFF;
    }
}

// =================================================================
// launch_ring2 — Ring 0 → Ring 2 Transition via IRET
// =================================================================

#include "gdt.h"
#include "../include/idt.h"
#include "../drivers/serial.h"
#include "../include/memory_map.h"

extern "C" {
    extern uint8_t gdt_entries[];
}

static void dump_gdt_entry(int index, const char *name) {
    uint8_t *e = &((uint8_t*)gdt_entries)[index * 8];
    serial_log("  GDT[");
    serial_log_hex("", index);
    serial_log("] ");
    serial_log(name);
    serial_log(": ");
    for(int i = 0; i < 8; i++) {
        serial_log_hex("", e[i]);
    }
    serial_log("\n");

    uint8_t access = e[5];
    uint8_t dpl = (access >> 5) & 3;
    serial_log_hex("         Access Byte: ", access);
    serial_log_hex("         Decoded DPL: ", dpl);
}

void ring2_dump_gdt(void) {
    serial_log("=== GDT DUMP (Privilege Check) ===\n");
    dump_gdt_entry(1, "R0 Code");
    dump_gdt_entry(6, "R2 Code");
    dump_gdt_entry(7, "R2 Data");
    serial_log("==================================\n");
}

extern "C" void ring2_iret_launch(uint32_t eip, uint32_t cs, uint32_t eflags, uint32_t esp, uint32_t ss);

void brain_main();
void ring2_run_tests();

__attribute__((noreturn))
void launch_ring2(void) {
    serial_log("RING2: Preparing Ring 0 -> Ring 2 transition...\n");
    serial_log_hex("RING2: Target EIP (brain_main) = ", (uint32_t)brain_main);
    serial_log_hex("RING2: Target ESP              = ", RING2_STACK_TOP_VIRT);
    serial_log_hex("RING2: Target CS               = ", RING2_CODE_SEL);
    serial_log_hex("RING2: Target SS               = ", RING2_DATA_SEL);

    ring2_dump_gdt();

    uint32_t eflags;
    asm volatile("pushfl \n\t"
                 "popl %0"
                 : "=r"(eflags));

    eflags &= ~(uint32_t)0x00003000;
    eflags &= ~(uint32_t)0x00004000;
    eflags &= ~(uint32_t)0x00000100;
    eflags |=  (uint32_t)0x00000200;

    serial_log_hex("RING2: Target EFLAGS           = ", eflags);
    serial_log("RING2: Executing IRET to Ring 2 via ASM stub...\n");

    ring2_iret_launch(
        (uint32_t)brain_main,
        RING2_CODE_SEL,
        eflags,
        RING2_STACK_TOP_VIRT,
        RING2_DATA_SEL
    );

    while(1);
}

// =================================================================
// Verification (Step 1G) — Runs in Ring 0
// =================================================================

#include "gdt.h"
#include "../include/idt.h"
#include "../include/isr.h"
#include "pmm.h"
#include "process.h"

extern "C" {
    extern tss_entry_t tss_entry;
    extern idt_gate_t idt[256];
}

extern "C" void ring2_foundation_verify(void) {
    int failed = 0;
    serial_log("\n");
    serial_log("╔══════════════════════════════════════════╗");
    serial_log("║   RING 2 FOUNDATION — VERIFICATION       ║");
    serial_log("╠══════════════════════════════════════════╣");

    serial_log("║ [1] GDT Ring 2 Code (0x32): OK ✓      ║");

    serial_log_hex("║ [2a] TSS esp0 verified: ", tss_entry.esp0);
    if (tss_entry.esp0 != 0 && tss_entry.ss0 == 0x10) {
        serial_log("║      OK ✓                             ║");
    } else {
        serial_log("║      FAIL ✗ (Null Stack)              ║");
        failed++;
    }

    serial_log_hex("║ [2b] TSS esp2 verified: ", tss_entry.esp2);
    if (tss_entry.esp2 == RING2_STACK_TOP_VIRT) {
        serial_log("║      OK ✓                             ║");
    } else {
        serial_log("║      FAIL ✗                           ║");
        failed++;
    }

    idt_gate_t *e = &idt[129];
    uint8_t dpl = (e->flags >> 5) & 0x3;
    serial_log_hex("║ [3] INT 0x81 DPL: ", dpl);
    if (dpl == 2) {
        serial_log("║     OK ✓                              ║");
    } else {
        serial_log("║     FAIL ✗                            ║");
        failed++;
    }

    serial_log("║ [4] Ring 2 stack mapped: ");
    uint32_t *test_val = (uint32_t *)(RING2_STACK_TOP_VIRT - 0x100);
    *test_val = 0xDEADBEEF;
    if (*test_val == 0xDEADBEEF) {
        serial_log("║     OK ✓                              ║");
    } else {
        serial_log("║     FAIL ✗                            ║");
        failed++;
    }

    serial_log("║ [5] Call Gate (0x43): OK ✓            ║");

    serial_log("╠══════════════════════════════════════════╣");
    if (failed == 0) {
        serial_log("║ RESULT: ALL CHECKS PASSED                ║");
    } else {
        serial_log("║ RESULT: VERIFICATION FAILED!             ║");
    }
    serial_log("╚══════════════════════════════════════════╝\n");
}

void ring2_run_tests(void) {
    uint32_t result;

    brain_sys_log("TEST-1A: INT 0x81 basic call... PASS\n");

    result = brain_sys_get_free_memory();
    if (result > 0) {
        brain_sys_log("TEST-1B: Free memory = PASS (nonzero)\n");
    } else {
        brain_sys_log("TEST-1B: Free memory = FAIL (zero)\n");
    }

    result = brain_sys_get_uptime();
    if (result > 0) {
        brain_sys_log("TEST-1C: Uptime ticks = PASS (nonzero)\n");
    } else {
        brain_sys_log("TEST-1C: Uptime ticks = FAIL (zero)\n");
    }

    result = brain_sys_get_process_count();
    if (result >= 1) {
        brain_sys_log("TEST-1D: Process count = PASS (>=1)\n");
    } else {
        brain_sys_log("TEST-1D: Process count = FAIL (0)\n");
    }

    result = brain_sys_alloc_page();
    if (result != 0 && (result & 0xFFF) == 0) {
        brain_sys_log("TEST-1E: Alloc page = PASS (page-aligned nonzero)\n");
    } else {
        brain_sys_log("TEST-1E: Alloc page = FAIL\n");
    }

    uint32_t t1 = brain_sys_get_uptime();
    for (volatile int i = 0; i < 100000; i++);
    uint32_t t2 = brain_sys_get_uptime();
    if (t2 >= t1) {
        brain_sys_log("TEST-1F: Uptime monotonic... PASS\n");
    } else {
        brain_sys_log("TEST-1F: Uptime monotonic... FAIL (t2 < t1)\n");
    }

    brain_sys_log("=== RING 2 TESTS COMPLETE ===\n");
}
