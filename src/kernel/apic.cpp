#include "apic.h"
#include "../drivers/acpi.h"
#include "../drivers/serial.h"
#include "../include/io.h"
#include "../include/string.h"
#include "paging.h"
#include "pae.h"

extern "C" {

uint32_t lapic_base = 0;
uint32_t ioapic_base = 0;
uint32_t total_cpus = 0;
uint8_t cpu_apic_ids[MAX_CPUS] = {0};

static void lapic_write(uint32_t reg, uint32_t data) {
    uint32_t val;
    val = (uint32_t)reg;
    *(volatile uint32_t*)(lapic_base + val) = data;
}

static uint32_t lapic_read(uint32_t reg) {
    uint32_t val;
    val = (uint32_t)reg;
    return *(volatile uint32_t*)(lapic_base + val);
}

uint32_t cpu_lapic_id = 0;

void lapic_init_silent() {
  // If lapic_base is already discovered (by BSP), we can proceed to enable on APs
  if (!lapic_base) {
    // If we're an AP and base isn't set, we assume the default (0xFEE00000)
    // because ACPI discovery is a BSP-only task.
    lapic_base = 0xFEE00000;
  }

  // Spurious Interrupt Vector - Bit 8 set karke APIC chalu karo
  lapic_write(LAPIC_SPURIOUS, lapic_read(LAPIC_SPURIOUS) | 0x1FF);

  // CRITICAL: Task Priority Register ko 0 karo, nahi toh interrupts drop ho jayenge
  lapic_write(LAPIC_TPR, 0);
}

void lapic_init() {
  if (get_cpu_index() == 0) serial_log("APIC: Initializing Local APIC...");
  lapic_init_silent();
}

uint8_t lapic_get_id() {
    if (!lapic_base) return 0;
    return (uint8_t)(lapic_read(LAPIC_ID) >> 24);
}

uint32_t get_cpu_index() {
  uint8_t id = lapic_get_id();
  for (uint32_t i = 0; i < total_cpus; i++) {
    if (cpu_apic_ids[i] == id)
      return i;
  }
  return 0; // Fallback to BSP
}

void lapic_eoi() { lapic_write(LAPIC_EOI, 0); }

static void ioapic_write(uint32_t reg, uint32_t value) {
  *(volatile uint32_t *)(ioapic_base) = reg;
  *(volatile uint32_t *)(ioapic_base + 0x10) = value;
}

static uint32_t ioapic_read(uint32_t reg) {
  *(volatile uint32_t *)(ioapic_base) = reg;
  return *(volatile uint32_t *)(ioapic_base + 0x10);
}

static acpi_madt_iso_t *isos[16] = {0};

static int ioapic_initialized = 0;
void ioapic_init() {
  if (ioapic_initialized) return;
  ioapic_initialized = 1;

  acpi_madt_t *madt = (acpi_madt_t *)acpi_find_table("APIC");
  if (!madt)
    return;

  uint8_t *ptr = madt->entries;
  uint8_t *end = (uint8_t *)madt + madt->header.length;

  total_cpus = 0; // Guard against existing increments
  while (ptr < end) {
    acpi_madt_entry_t *entry = (acpi_madt_entry_t *)ptr;
    if (entry->type == 0) { // Processor Local APIC
      acpi_madt_lapic_t *lapic = (acpi_madt_lapic_t *)ptr;
      if (lapic->flags & 1) { // Enabled
        if (total_cpus < MAX_CPUS) {
          cpu_apic_ids[total_cpus] = lapic->apic_id;
          total_cpus++;
        }
      }
    } else if (entry->type == 1) { // IO-APIC
      acpi_madt_io_apic_t *io = (acpi_madt_io_apic_t *)ptr;
      ioapic_base = io->io_apic_addr;
      serial_log_hex("IO-APIC: Base at ", ioapic_base);
    } else if (entry->type == 2) { // ISO
      acpi_madt_iso_t *iso = (acpi_madt_iso_t *)ptr;
      if (iso->irq_source < 16) {
        isos[iso->irq_source] = iso;
        serial_log_hex("IO-APIC: Found ISO for IRQ ", iso->irq_source);
      }
    }
    ptr += entry->length;
  }

  serial_log_hex("APIC: Detected Processors count: ", total_cpus);

  // CRITICAL: Ensure we know the BSP's hardware APIC ID for the destination field
  cpu_lapic_id = lapic_get_id();
  serial_log_hex("APIC: BSP Hardware APIC ID: ", cpu_lapic_id);

  // Purane PIC ko chutti de do (Disable legacy PIC)
  outb(0x21, 0xFF);
  outb(0xA1, 0xFF);
  serial_log("PIC: Disabled.");

  // Default redirection: 16 purane IRQs ko vector 32-47 pe map karo
  // Dhyan rahe ye UNMASKED hone chahiye (bit 16 is 0)
  // Delivery mode: Fixed (000), Destination mode: Physical (0)
  for (int i = 0; i < 16; i++) {
    uint32_t vector = 32 + i;

    // Low 32 bits ka hisaab:
    // bit 0-7: vector
    // bit 8-10: delivery mode (000 = Fixed)
    // bit 11: destination mode (0 = Physical)
    // bit 12: delivery status (RO)
    // bit 13: polarity (0 = Active High, 1 = Active Low)
    // bit 14: remote IRR (RO)
    // bit 15: trigger mode (0 = Edge, 1 = Level)
    // bit 16: mask (0 = Unmasked, 1 = Masked)
    uint32_t low_part =
        vector | 0x10000; // Default: Masked, Edge, Active High, Fixed

    uint32_t target_gsi = i;
    if (isos[i]) {
      target_gsi = isos[i]->global_system_interrupt;
      // Bahut saare ISOs (jaise IRQ0 -> GSI 2) active high/edge hote hain
      // par agar flags kuch aur bole toh wo maano
      // iso->flags: bit 0-1 (Polarity), bit 2-3 (Trigger Mode)
      // Polarity: 1=AH, 3=AL. Trigger: 1=Edge, 3=Level.
      if ((isos[i]->flags & 3) == 3)
        low_part |= (1 << 13); // Active Low
      if (((isos[i]->flags >> 2) & 3) == 3)
        low_part |= (1 << 15); // Level Triggered

      serial_log_hex("IO-APIC: Routing IRQ ", i);
      serial_log_hex("IO-APIC: To GSI ", target_gsi);
    }

    uint64_t entry = low_part;
    entry |= ((uint64_t)cpu_lapic_id << 56);

    ioapic_set_irq(target_gsi, entry);
  }
  serial_log("IO-APIC: Legacy IRQs 0-15 unmasked and routed.");
}

void ioapic_set_irq(uint8_t irq, uint64_t vector_data) {
  uint32_t low = (uint32_t)vector_data;
  uint32_t high = (uint32_t)(vector_data >> 32);

  ioapic_write(IOAPIC_REDTBL + irq * 2, low);
  ioapic_write(IOAPIC_REDTBL + irq * 2 + 1, high);
}

void ioapic_set_mask(uint8_t irq, bool masked) {
  if (irq >= 16)
    return;
  uint32_t gsi = irq;
  if (isos[irq]) {
    gsi = isos[irq]->global_system_interrupt;
  }

  uint32_t low = ioapic_read(IOAPIC_REDTBL + gsi * 2);
  uint32_t high = ioapic_read(IOAPIC_REDTBL + gsi * 2 + 1);

  if (masked)
    low |= (1 << 16);
  else
    low &= ~(1 << 16);
  
  if (irq == 0) {
      serial_log_hex("IOAPIC: [TIMER] UNMASKING IRQ 0, GSI: ", gsi);
      serial_log_hex("IOAPIC: [TIMER] Entry Low (Vector/Mask): ", low);
      serial_log_hex("IOAPIC: [TIMER] Entry High (Dest ID):   ", high);
  }

  if (irq == 12) {
      serial_log_hex("IOAPIC: [MOUSE] UNMASKING IRQ 12, GSI: ", gsi);
      serial_log_hex("IOAPIC: [MOUSE] Entry Low (Vector/Mask): ", low);
      serial_log_hex("IOAPIC: [MOUSE] Entry High (Dest ID):   ", high);
  }

  ioapic_write(IOAPIC_REDTBL + gsi * 2, low);
}

void ioapic_route_irq(uint8_t irq, uint8_t apic_id) {
  if (irq >= 16) return;
  uint32_t gsi = irq;
  if (isos[irq]) {
    gsi = isos[irq]->global_system_interrupt;
  }

  uint32_t low = ioapic_read(IOAPIC_REDTBL + gsi * 2);
  // Delivery Mode: Fixed (000), Destination Mode: Physical (0)
  // Mask must be 0 for it to work. Vector is in low 8 bits.
  low &= 0x0001FFFF; 
  
  uint32_t high = (uint32_t)apic_id << 24;

  serial_log_hex("IOAPIC: Steering IRQ ", irq);
  serial_log_hex("IOAPIC: To APIC ID ", apic_id);

  ioapic_write(IOAPIC_REDTBL + gsi * 2, low);
  ioapic_write(IOAPIC_REDTBL + gsi * 2 + 1, high);
}

extern "C" {
extern char ap_trampoline_binary[];
extern uint32_t ap_trampoline_size;
extern void ap_kernel_entry();
extern uint32_t g_pdpt_phys_addr;
}

extern "C" uint32_t ap_kernel_stacks[MAX_CPUS];
uint32_t ap_kernel_stacks[MAX_CPUS];
static uint8_t ap_stacks[MAX_CPUS][65536] __attribute__((aligned(16)));

void smp_init() {
  if (total_cpus <= 1) return;

  serial_log("SMP: Initializing Symmetric Multiprocessing...");

  // 1. Copy trampoline to 0x1000
  memcpy((void *)0x1000, ap_trampoline_binary, ap_trampoline_size);

  // 2. Set pointers in the trampoline (relative to 0x1000)
  volatile uint32_t *ap_pdpt_ptr  = (volatile uint32_t *)(0x1008);
  volatile uint32_t *ap_stack_ptr = (volatile uint32_t *)(0x100C);
  volatile uint32_t *ap_entry_ptr = (volatile uint32_t *)(0x1010);
  volatile uint32_t *ap_ready_flag = (volatile uint32_t *)(0x1000 + 0x14);

  // Determine Entry Point (Already in Higher-Half)
  uint32_t entry_addr = (uint32_t)(uintptr_t)ap_kernel_entry;

  *ap_pdpt_ptr = g_pdpt_phys_addr;
  *ap_entry_ptr = entry_addr;

  serial_log_hex("SMP: PDPT Physical:  ", g_pdpt_phys_addr);
  serial_log_hex("SMP: Entry Point:    ", entry_addr);
  
  // Read back and verify
  serial_log_hex("SMP: Param PDPT Verify:  ", *(uint32_t*)0x1008);
  serial_log_hex("SMP: Param Entry Verify: ", *(uint32_t*)0x1010);

  for (uint32_t i = 0; i < total_cpus; i++) {
    uint8_t id = cpu_apic_ids[i];
    
    // BSP stack is managed elsewhere, but we track AP stacks
    if (id != cpu_lapic_id) {
        // Use a conservative, guaranteed identity-mapped range for initial boot stacks
        // 0x20000 (128KB) and up is safe and avoids higher-half mapping traps
        ap_kernel_stacks[i] = 0x20000 + (i * 0x4000); 
    } else {
        ap_kernel_stacks[i] = 0; // BSP stack handled by kernel_entry
    }

    if (id == cpu_lapic_id) continue; // Skip BSP

    serial_log_hex("SMP: Booting Core APIC ID: ", id);
    
    // Reset handshake and provide stack for this AP
    extern volatile uint32_t g_ap_handshake;
    g_ap_handshake = 0;
    *ap_stack_ptr = ap_kernel_stacks[i];
    *ap_ready_flag = 0; // Force-reset flag before IPI
    
    // Ensure write visibility
    asm volatile("mfence" ::: "memory");

    serial_log_hex("SMP: Param Stack Verify: ", *(uint32_t*)0x100C);

    // --- IPI Sequence (MP Specification Compliant) ---
    // 1. INIT IPI
    lapic_write(LAPIC_ICR_HIGH, (uint32_t)id << 24);
    lapic_write(LAPIC_ICR_LOW, 0x00004500); // Level Assert, INIT

    // Wait 10ms for INIT to take effect
    for (volatile int d = 0; d < 2000000; d++);

    // 2. STARTUP IPI 1 (Vector 0x01 -> 0x1000)
    lapic_write(LAPIC_ICR_HIGH, (uint32_t)id << 24);
    lapic_write(LAPIC_ICR_LOW, 0x00004601);

    // Wait 200us (approx)
    for (volatile int d = 0; d < 100000; d++);

    // 3. STARTUP IPI 2 (Safety repeat)
    lapic_write(LAPIC_ICR_HIGH, (uint32_t)id << 24);
    lapic_write(LAPIC_ICR_LOW, 0x00004601);

    // Wait for AP to signal ready (Phys 0x1014)
    serial_log("SMP: Waiting for core handshake (0xB002)...");
    uint32_t timeout = 50000000;
    while (*ap_ready_flag != 0xB002 && timeout > 0) {
        timeout--;
        asm volatile("pause");
        asm volatile("" ::: "memory");
    }

    if (*ap_ready_flag == 0xB002) {
        serial_log("SMP: Core mobilized successfully.");
        // Inter-Core Settling Delay: Give the newly mobilized core time to stabilize
        for (volatile int s = 0; s < 100000; s++) asm volatile("pause");
    } else {
        serial_log("SMP: ERROR - Core timeout (Phys Stage 2)!");
    }
  }
}

void apic_map_hardware() {
  // Standard APIC addresses (ACPI discovery se pehle map kar lo)
  // LAPIC 0xFEE00000 pe, IO-APIC 0xFEC00000 pe hota hai
  pae_map_page(0xFEE00000, 0xFEE00000, PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE | PAE_FLAG_PCD | PAE_FLAG_PWT); // LAPIC
  pae_map_page(0xFEC00000, 0xFEC00000, PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE | PAE_FLAG_PCD | PAE_FLAG_PWT); // IO-APIC
}

// SMP IPI Primitives
void smp_send_ipi(uint8_t target_apic_id, uint8_t vector);
void smp_send_ipi_all_but_self(uint8_t vector);
void smp_tlb_shootdown(uint32_t virt);
void smp_halt_others();
void smp_reschedule(uint8_t target_cpu_idx);

void smp_send_ipi(uint8_t target_apic_id, uint8_t vector) {
    // 1. Wait for previous IPI to finish (Check Delivery Status bit 12)
    while (lapic_read(LAPIC_ICR_LOW) & (1 << 12));

    // 2. Set target in High register
    lapic_write(LAPIC_ICR_HIGH, (uint32_t)target_apic_id << 24);

    // 3. Send IPI (Fixed delivery, physical destination)
    lapic_write(LAPIC_ICR_LOW, vector | 0x00004000); // 0x4000 = Asserted
}

void smp_send_ipi_all_but_self(uint8_t vector) {
    // 1. Wait for previous IPI to finish
    while (lapic_read(LAPIC_ICR_LOW) & (1 << 12));

    // 2. Send IPI with All-Excluding-Self shorthand (bits 18-19 = 11)
    lapic_write(LAPIC_ICR_LOW, vector | 0x000C4000); 
}

void smp_tlb_shootdown(uint32_t virt) {
    // 1. Local Invalidate
    asm volatile("invlpg (%0)" ::"r"(virt) : "memory");

    // 2. Remote Invalidate (only if other cores are active)
    if (total_cpus > 1) {
        smp_send_ipi_all_but_self(IPI_VECTOR_TLB_SHOOTDOWN);
    }
}

void smp_halt_others() {
    if (total_cpus > 1) {
        smp_send_ipi_all_but_self(IPI_VECTOR_PANIC);
    }
}

void smp_reschedule(uint8_t target_cpu_idx) {
    if (target_cpu_idx < total_cpus && target_cpu_idx < MAX_CPUS) {
        uint8_t apic_id = cpu_apic_ids[target_cpu_idx];
        smp_send_ipi(apic_id, IPI_VECTOR_SCHEDULE);
    }
}

} // extern "C"
