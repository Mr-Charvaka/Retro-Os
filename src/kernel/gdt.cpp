#include "gdt.h"
#include "apic.h"
#include "../drivers/serial.h"
#include "../include/string.h"
#include "../include/memory_map.h"

extern "C" {

extern void gdt_flush(uint32_t);
extern void tss_flush();

gdt_entry_t gdt_entries[MAX_CPUS][GDT_ENTRY_COUNT];
gdt_ptr_t gdt_ptr[MAX_CPUS];
tss_entry_t tss_entry[MAX_CPUS];

// Dedicated Ring 0 stack for hardware-driven privilege transitions (Ring 3/2 -> Ring 0).
// Each CPU gets its own bootstrap stack.
static uint8_t tss_esp0_stack[MAX_CPUS][8192] __attribute__((aligned(16)));

void gdt_set_call_gate(int cpu, int32_t num, uint32_t offset, uint16_t selector,
                       uint8_t param_count, uint8_t access);

void gdt_set_gate(int cpu, int32_t num, uint32_t base, uint32_t limit, uint8_t access,
                  uint8_t gran) {
  gdt_entries[cpu][num].base_low = (base & 0xFFFF);
  gdt_entries[cpu][num].base_middle = (base >> 16) & 0xFF;
  gdt_entries[cpu][num].base_high = (base >> 24) & 0xFF;

  gdt_entries[cpu][num].limit_low = (limit & 0xFFFF);
  gdt_entries[cpu][num].granularity = (limit >> 16) & 0x0F;

  gdt_entries[cpu][num].granularity |= gran & 0xF0;
  gdt_entries[cpu][num].access = access;
}

void write_tss(int cpu, int32_t num, uint16_t ss0, uint32_t esp0) {
  uint32_t base = (uint32_t)&tss_entry[cpu];
  uint32_t limit = base + sizeof(tss_entry_t);

  gdt_set_gate(cpu, num, base, limit, 0xE9, 0x00);

  // Zero ENTIRE TSS
  memset(&tss_entry[cpu], 0, sizeof(tss_entry_t));

  tss_entry[cpu].ss0  = ss0;
  // If esp0 is provided, use it, otherwise use bootstrap stack
  tss_entry[cpu].esp0 = esp0 ? esp0 : (uint32_t)&tss_esp0_stack[cpu][8192];

  tss_entry[cpu].ss2  = RING2_DATA_SEL;
  tss_entry[cpu].esp2 = RING2_STACK_TOP_VIRT;

  tss_entry[cpu].cs = 0x0b;
  tss_entry[cpu].ss = tss_entry[cpu].ds = tss_entry[cpu].es = tss_entry[cpu].fs = tss_entry[cpu].gs = 0x13;

  tss_entry[cpu].iomap_base = sizeof(tss_entry_t);
}

void gdt_set_call_gate(int cpu, int32_t num, uint32_t offset, uint16_t selector,
                       uint8_t param_count, uint8_t access) {
    uint8_t *entry = (uint8_t *)&gdt_entries[cpu][num];

    entry[0] = (uint8_t)(offset & 0xFF);
    entry[1] = (uint8_t)((offset >> 8) & 0xFF);
    entry[2] = (uint8_t)(selector & 0xFF);
    entry[3] = (uint8_t)((selector >> 8) & 0xFF);
    entry[4] = param_count & 0x1F;
    entry[5] = access;
    entry[6] = (uint8_t)((offset >> 16) & 0xFF);
    entry[7] = (uint8_t)((offset >> 24) & 0xFF);
}

void init_gdt() {
  serial_log("GDT: Initializing (BSP)...");
  int cpu = 0; // BSP is always 0 in this logic context
  
  gdt_ptr[cpu].limit = (sizeof(gdt_entry_t) * GDT_ENTRY_COUNT) - 1;
  gdt_ptr[cpu].base = (uint32_t)&gdt_entries[cpu];

  gdt_set_gate(cpu, 0, 0, 0, 0, 0);                // [0] Null
  gdt_set_gate(cpu, 1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // [1] Ring 0 Code
  gdt_set_gate(cpu, 2, 0, 0xFFFFFFFF, 0x92, 0xCF); // [2] Ring 0 Data
  gdt_set_gate(cpu, 3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // [3] Ring 3 Code
  gdt_set_gate(cpu, 4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // [4] Ring 3 Data
  write_tss(cpu, 5, 0x10, 0x0);                     // [5] TSS
  gdt_set_gate(cpu, 6, 0, 0xFFFFFFFF, 0xDA, 0xCF); // [6] Ring 2 Code
  gdt_set_gate(cpu, 7, 0, 0xFFFFFFFF, 0xD2, 0xCF); // [7] Ring 2 Data

  extern void brain_request_handler_entry(void);
  gdt_set_call_gate(cpu, 8, (uint32_t)brain_request_handler_entry, RING2_CODE_SEL, 2, 0xEC);

  gdt_flush((uint32_t)&gdt_ptr[cpu]);
  tss_flush();
  
  serial_log("GDT: BSP Context Loaded.");
}

void gdt_init_ap(int cpu, uint32_t kernel_stack) {
  gdt_ptr[cpu].limit = (sizeof(gdt_entry_t) * GDT_ENTRY_COUNT) - 1;
  gdt_ptr[cpu].base = (uint32_t)&gdt_entries[cpu];

  // Copy BSP GDT as baseline
  memcpy(&gdt_entries[cpu], &gdt_entries[0], sizeof(gdt_entry_t) * GDT_ENTRY_COUNT);

  // Overwrite TSS entry for THIS CPU
  write_tss(cpu, 5, 0x10, kernel_stack);

  gdt_flush((uint32_t)&gdt_ptr[cpu]);
  tss_flush();
}

void set_kernel_stack(uint32_t stack) {
  int cpu = get_cpu_index();
  tss_entry[cpu].esp0 = stack;
}

} // extern "C"
