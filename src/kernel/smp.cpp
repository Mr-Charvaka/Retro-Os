#include "apic.h"
#include "gdt.h"
#include "../include/idt.h"
#include "../drivers/serial.h"
#include "../include/string.h"
#include "process.h"

extern "C" {

extern "C" uint32_t ap_kernel_stacks[MAX_CPUS];
volatile uint32_t *ap_ready_flag = (volatile uint32_t *)(0x1000 + 0x14);
volatile uint32_t g_ap_ticket_counter = 1; // BSP is 0, first AP is 1
volatile uint32_t g_smp_ready = 0;         // 0 = Parked, 1 = Active
volatile uint32_t g_ap_handshake = 0;      // 0 = Waiting, 0xB002 = Ready

void ap_kernel_entry() {
    // 0. ABSOLUTE FIRST ACTION: Signal arrival to BSP
    // Use the global semaphore for guaranteed visibility
    extern volatile uint32_t g_ap_handshake;
    g_ap_handshake = 0xB002;
    *ap_ready_flag = 0xB002; // Dual-signal for safety

    // 1. Initialize Local APIC for this core IMMEDIATELY
    // Use SILENT version to avoid serial port deadlocks with BSP
    void lapic_init_silent();
    lapic_init_silent();

    // 2. Identification (FAST-TRACK ATOMIC TICKETING)
    int cpu = __sync_fetch_and_add(&g_ap_ticket_counter, 1);
    if (cpu >= MAX_CPUS) cpu = MAX_CPUS - 1;

    uint32_t stack = ap_kernel_stacks[cpu];

    // 3. Hardware Context Loading
    gdt_init_ap(cpu, stack);
    idt_load();
    lapic_init(); // Re-verify LAPIC after GDT/IDT
    
    // 4. Final Mobilization Signal (BITMASK)
    extern volatile uint32_t g_cores_online;
    g_cores_online |= (1 << cpu);
    
    // 5. Handshake complete: Exit Stage 2
    serial_log_hex("SMP: Core mobilized and online. CPU Index: ", cpu);
    *ap_ready_flag = 0xB002;

    
    // 6. PARKING LOOP: Wait for BSP to signal system-wide readiness
    extern volatile uint32_t g_smp_ready;
    while (g_smp_ready == 0) {
        asm volatile("pause");
        asm volatile("" ::: "memory");
    }

    // 7. Initialize scheduling and enter task loop
    init_ap_scheduling();

    // Enter the global scheduler to start picking up tasks
    while (1) {
        kernel_yield(); // Voluntarily enter the scheduler
        asm volatile("sti; hlt");
    }
}

} // extern "C"
