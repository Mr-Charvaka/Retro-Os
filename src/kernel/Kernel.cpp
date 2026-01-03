//===================================================================
// Kernel.cpp – core kernel entry point
//===================================================================

#include "../drivers/acpi.h"
#include "../drivers/bga.h"
#include "../drivers/fat16.h"
#include "../drivers/graphics.h"
#include "../drivers/hpet.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/pci.h"
#include "../drivers/rtc.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "../drivers/vga.h"

// From src/include
#include "idt.h"
#include "io.h"
#include "irq.h"
#include "isr.h"
#include "string.h"
#include "types.h"
#include "vfs.h"

// From src/kernel
#include "gdt.h"
#include "gui.h"
#include "heap.h"
#include "memory.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "slab.h"
#include "socket.h"
#include "syscall.h"
#include "tsc.h"
#include "tty.h"

extern "C" void cpp_kernel_entry();
extern "C" void __cxx_global_ctor_init();

//-------------------------------------------------------------------
// Hardware Exception Handlers
//-------------------------------------------------------------------
static void enable_fpu(void) {
  asm volatile("clts");
  uint32_t cr0;
  asm volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 &= ~0x2;
  cr0 &= ~0x200000;
  asm volatile("mov %0, %%cr0" ::"r"(cr0));
  asm volatile("fninit");
}

void isr8_handler(registers_t *regs) {
  (void)regs;
  serial_log("FATAL: DOUBLE FAULT!");
  for (;;)
    ;
}

void isr9_handler(registers_t *regs) {
  (void)regs;
  serial_log("EXCEPTION: #NM (FPU not available)");
  enable_fpu();
}

//===================================================================
// Kernel main
//===================================================================
int main() {
  init_serial();
  serial_log("KERNEL: Main Entry point reached.");
  serial_log("KERNEL: Booting Higher-Half Retro-OS...");

  // 0. Early CPU Setup to catch faults
  serial_log("KERNEL: Init GDT...");
  init_gdt();
  serial_log("KERNEL: Init ISRs...");
  isr_install();

  // 1. Core Memory setup (Before almost everything else)
  serial_log("KERNEL: Init Placement Heap at 4MB...");
  init_memory(PHYS_TO_VIRT(0x00400000)); // Move to 4MB

  serial_log("KERNEL: Init PMM at 5MB...");
  uint32_t mem_size = 512 * 1024 * 1024;
  uint32_t *bitmap_addr = (uint32_t *)PHYS_TO_VIRT(0x00500000); // Move to 5MB
  pmm_init(mem_size, bitmap_addr);

  pmm_mark_region_used(0x0, 0x100000);      // Low Memory
  pmm_mark_region_used(0x100000, 0x300000); // Kernel (Reserved up to 4MB)
  pmm_mark_region_used(0x400000, 0x100000); // Placement Heap (at 4MB)
  pmm_mark_region_used(VIRT_TO_PHYS(bitmap_addr), 16384);
  pmm_mark_region_used(0x01000000, 0x20000000); // Kernel Heap Physical (512MB)

  // 3. Initialize full paging (Transitions from boot mapping to unified map)
  serial_log("KERNEL: Init Paging...");
  init_paging();

  // 4. Post-paging setup
  init_syscalls();
  register_interrupt_handler(8, (isr_t)isr8_handler);
  register_interrupt_handler(9, (isr_t)isr9_handler);

  serial_log("KERNEL: Interrupts & Syscalls & GDT Ready.");

  // 5. Hardware Interface initialization
  irq_install(); // ACPI / APIC / IO-APIC

  // Initialize input before enabling interrupts
  init_keyboard();
  init_mouse();

  serial_log("KERNEL: Enabling global interrupts...");
  asm volatile("sti");

  hpet_init();
  tsc_calibrate();
  serial_log("KERNEL: Drivers & Timers Active.");

  enable_fpu();

  // 6. Heap & Filesystem - 512MB heap from 16MB to 528MB
  init_kheap(PHYS_TO_VIRT(0x01000000), PHYS_TO_VIRT(0x21000000),
             PHYS_TO_VIRT(0x21000000));
  set_heap_status(1);
  slab_init();
  extern int slab_is_initialized;
  slab_is_initialized = 1;

  // Initialize C++ global constructors (required for vtables)
  __cxx_global_ctor_init();

  fat16_init();
  vfs_root = fat16_vfs_init();
  vfs_dev = devfs_init();
  socket_init();
  tty_init();

  // 7. Graphics (BGA)
  serial_log("KERNEL: Scanning PCI for BGA...");
  u32 fb_addr = pci_get_bga_bar0();

  if (fb_addr == 0) {
    serial_log("KERNEL: FATAL - BGA Not Found! Attempting VBE fallback.");
    // TODO: implement VBE fallback if needed
  } else {
    serial_log_hex("KERNEL: BGA LFB Address: ", fb_addr);
    // Set video mode; currently bga_set_video_mode has no return value
    bga_set_video_mode(1024, 768, 32);

    // Map a larger framebuffer region (e.g., 16 MB) to be safe
    // Ensure this mapping happens AFTER init_paging()
    const uint32_t fb_size = 16 * 1024 * 1024; // 16 MB
    const uint32_t pages = fb_size / 4096;
    serial_log("KERNEL: Mapping VRAM (16 MB)...");
    for (uint32_t i = 0; i < pages; i++) {
      paging_map(fb_addr + (i * 4096), fb_addr + (i * 4096), 3);
    }

    init_graphics(fb_addr);

    // 8. User Space execution
    init_multitasking();

    serial_log("KERNEL: Starting GUI...");
    gui_init();
    serial_log("KERNEL: GUI Ready.");

    // Start user space
    create_user_process("INIT.ELF");
    init_timer(50);
    serial_log("KERNEL: Higher-Half Kernel Running. Idle Loop Active.");
  }

  while (1) {
    asm volatile("hlt");
  }
}