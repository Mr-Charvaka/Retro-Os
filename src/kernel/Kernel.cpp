// Contracts aur Interfaces uthao shuru mein
#include "../include/Contracts.h"
#include "../include/KernelInterfaces.h"
#include "../include/fs_phase.h"

#include "../drivers/acpi.h"
#include "../drivers/ac97.h"
#include "../drivers/ahci.h"
#include "../drivers/bga.h"
#include "../drivers/fat32.h"
#include "../drivers/graphics.h"
#include "../drivers/hpet.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/pci.h"
#include "../drivers/rtc.h"
#include "../drivers/serial.h"
#include "../drivers/sb16.h"
#include "../drivers/timer.h"
#include "../drivers/vga.h"
#include "../drivers/block.h"


// Drivers ki fauj yahan hai
#include "../include/idt.h"
#include "../include/io.h"
#include "../include/irq.h"
#include "../include/isr.h"
#include "../include/string.h"
#include "../include/types.h"
#include "../include/vfs.h"

// From src/kernel
#include "e1000.h"
#include "gdt.h"
#include "heap.h"
#include "memory.h"
#include "net.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "ring2.h"
#include "brain/brain_mailbox.h"
#include "slab.h"
#include "socket.h"
#include "syscall.h"
#include "tsc.h"
#include "tty.h"
#include "e820.h"
#include "pmm_high.h"
#include "pae.h"   // NEW: PAE Paging System
#include "pae_security.h"

extern "C" void gui_main();
extern uint32_t *back_buffer;   // graphics se liya
extern "C" void swap_buffers(); // screen updates ke liye

extern "C" void get_mouse_state(int *x, int *y,
                                uint8_t *btn); // mouse.cpp se layenge
extern "C" void disk_allocator_init();
extern "C" void fs_init();
extern "C" void fs_undo();
extern "C" void fs_redo();
extern "C" void fs_restore(uint32_t);
extern "C" void fs_purge(uint32_t);
extern "C" void kernel_core_init();
extern "C" void kernel_advanced_init();
extern "C" int gui_create_file_real(const char *);
extern "C" void fs_delete(uint32_t);
extern "C" int vfs_mkdir(const char *path, uint32_t mode);
extern "C" int vfs_create(const char *path, int type);

/* 🧾 FS TRANSACTION CONTRACT */
#define MAX_PATH 256
enum FsOpType { FS_OPEN, FS_CREATE, FS_DELETE, FS_MOVE, FS_COPY, FS_RENAME };
struct FsTransaction {
  FsOpType type;
  char src[MAX_PATH];
  char dst[MAX_PATH];
  uint32_t flags;
};

extern "C" int sys_fs_transaction_impl(const FsTransaction *tx);

/* 🚀 BRIDGE (Kernel aur GUI ke beech ka pool) */
enum FsRequestType {
  FS_REQ_CREATE,
  FS_REQ_DELETE,
  FS_REQ_OPEN,
  FS_REQ_READDIR,
  FS_REQ_READ,
  FS_REQ_WRITE,
  FS_REQ_CLOSE
};

struct FsRequest {
  FsRequestType type;
  char path[MAX_PATH];
  void *buffer;
  uint32_t length;
};

struct FsResponse {
  int status;
  uint32_t out_count;
};

extern "C" int sys_fs_call(FsRequest *req, FsResponse *res);

/* 🧩 EXPLORER KERNEL CONTRACT BRIDGE */
#define MAX_NAME 64
enum SyscallID {
  SYS_CREATE_FILE,
  SYS_DELETE_FILE,
  SYS_RENAME_FILE,
  SYS_COPY_FILE,
  SYS_PASTE_FILE,
  SYS_GET_FILES
};

struct SyscallPacket {
  SyscallID id;
  char path[MAX_PATH];
  char name[MAX_NAME];
  char new_name[MAX_NAME];
  void *buffer;
  uint32_t buffer_size;
  bool is_dir;
};

extern "C" int kernel_syscall(SyscallPacket *p);

extern "C" void compositor_invalidate() {
  // Force a full redraw of the GUI loop if needed
}

extern "C" void *sys_get_framebuffer() { return (void *)back_buffer; }
extern "C" int sys_fb_width() { return 1024; }
extern "C" int sys_fb_height() { return 768; }
extern "C" int sys_fb_pitch() {
  return 1024 * 4;
} // bytes per line, hisaab fix hai
extern "C" void sys_fb_swap() { swap_buffers(); }
extern "C" void sys_get_mouse(int *x, int *y, int *btn) {
  uint8_t b;
  get_mouse_state(x, y, &b);
  *btn = (int)b;
}

extern "C" {
extern uint8_t cpu_apic_ids[];
void ioapic_route_irq(uint8_t irq, uint8_t apic_id);
}

// Rev S: AI Brain activation flag
volatile int g_brain_activated = 0;

extern "C" uint32_t tick;
extern "C" uint32_t sys_time_ms() {
  return tick * 10; // 100Hz = 10ms per tick
}

// Advanced network stack initialization
extern "C" void net_advanced_init();
extern "C" void net_print_status();
extern "C" void dhcp_check_lease();

extern "C" void net_thread() {
  net_init();

  // Initialize Advanced Network Stack (DNS, DHCP, HTTP, Sockets)
  serial_log("NET_THREAD: Initializing Advanced Network Stack...");
  net_advanced_init();
  net_print_status();

  uint32_t lease_check_timer = 0;

  while (1) {
    net_poll();

    // Periodically check DHCP lease renewal (every 60 seconds)
    if (++lease_check_timer > 3000) { // 60s at 50Hz poll rate
      lease_check_timer = 0;
      dhcp_check_lease();
    }

    schedule();
  }
}

extern "C" int sys_spawn(const char *path, char **argv) {
  return create_user_process(path, argv);
}

// Keyboard: aakhri scancode wapas karo ya -1 agar kuch nahi hai
static volatile int g_last_key = -1;
extern "C" void keyboard_set_last_key(int k) { g_last_key = k; }
extern "C" int sys_get_key() {
  int k = g_last_key;
  g_last_key = -1;
  return k;
}

extern "C" int sys_read_key(int *key, int *state) {
  int k = sys_get_key();
  if (k == -1)
    return 0;
  *key = k & 0x7F;
  *state =
      (k & 0x80) ? 0 : 1; // Basic scancode mapping: bit 7 ka matlab release
  return 1;
}

extern "C" int sys_is_dir(const char *path) {
  vfs_node_t *n = vfs_resolve_path(path);
  return (n && n->type == VFS_DIRECTORY) ? 1 : 0;
}

extern "C" int sys_move(const char *src, const char *dst) {
  (void)src;
  (void)dst;
  return 0;
}

extern "C" int sys_copy(const char *src, const char *dst) {
  (void)src;
  (void)dst;
  return 0;
}

extern "C" int sys_fs_transaction(const FsTransaction *tx) {
  // Mock transaction
  (void)tx;
  return 0;
}

extern "C" int sys_fs_call(FsRequest *req, FsResponse *res) {
  // Mock fs call
  (void)req;
  (void)res;
  return 0;
}

// VFS directory listing
extern "C" int sys_vfs_list(const char *path, char names[][64], int max) {
  vfs_node_t *node = vfs_resolve_path(path);
  if (!node)
    return 0;
  int count = 0;
  for (int i = 0; i < max; i++) {
    struct dirent *de = readdir_vfs(node, i);
    if (!de)
      break;
    if (de->d_name[0] == 0)
      break;
    for (int j = 0; j < 63 && de->d_name[j]; j++) {
      names[count][j] = de->d_name[j];
      names[count][j + 1] = 0;
    }
    count++;
  }
  return count;
}

static vfs_node_t *k_fd_table[32] = {0};
static uint64_t k_fd_offsets[32] = {0}; // File offset per kernel FD

// Helper to resolve handle to node
static vfs_node_t *resolve_k_fd(int fd) {
  if (fd >= 1000 && fd < 1032)
    return k_fd_table[fd - 1000];
  if (fd > 0x100000)
    return (vfs_node_t *)(uintptr_t)fd; // Direct pointer
  return nullptr;
}

extern "C" int sys_open(const char *path, int flags) {
  (void)flags;
  vfs_node_t *node = vfs_resolve_path(path);
  if (!node)
    return -1;

  for (int i = 0; i < 32; i++) {
    if (k_fd_table[i] == 0) {
      k_fd_table[i] = node;
      k_fd_offsets[i] = 0; // Reset offset on open
      return i + 1000; // Offset to avoid conflict with process FDs if shared
    }
  }
  return -1;
}

extern "C" int sys_readdir(int fd, uint32_t index, void *buf) {
  vfs_node_t *node = resolve_k_fd(fd);
  if (!node)
    return -1;

  struct dirent *de = readdir_vfs(node, index);
  if (!de)
    return -1;

  struct OutDe {
    char name[64];
    uint32_t type;
  } *out = (struct OutDe *)buf;

  int i = 0;
  while (i < 63 && de->d_name[i]) {
    out->name[i] = de->d_name[i];
    i++;
  }
  out->name[i] = 0;
  out->type = (uint32_t)de->d_type;
  return 0;
}

extern "C" int sys_close(int fd) {
  if (fd >= 1000 && fd < 1032) {
    k_fd_table[fd - 1000] = 0;
    k_fd_offsets[fd - 1000] = 0;
  }
  return 0;
}

extern "C" int sys_read(int fd, void *buf, uint32_t len) {
  vfs_node_t *node = resolve_k_fd(fd);
  if (!node)
    return -1;
  int idx = (fd >= 1000 && fd < 1032) ? (fd - 1000) : -1;
  uint64_t offset = (idx >= 0) ? k_fd_offsets[idx] : 0;
  int n = vfs_read(node, (uint32_t)offset, buf, len);
  if (n > 0 && idx >= 0) {
    k_fd_offsets[idx] += n;
  }
  return n;
}

extern "C" int sys_mkdir(const char *path, int perms) {
  (void)perms;
  return vfs_mkdir(path, 0755);
}

extern "C" int sys_stat(const char *path, void *stat_out) {
  vfs_node_t *node = vfs_resolve_path(path);
  if (!node)
    return -1;
  struct Stat {
    uint32_t size;
    uint32_t type;
  } *s = (Stat *)stat_out;
  s->size = (uint32_t)node->size;
  s->type = (node->type == VFS_DIRECTORY) ? 2 : 1;
  return 0;
}

static void enable_fpu(void) {
  asm volatile("clts");
  uint32_t cr0;
  asm volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 &= ~0x2;
  cr0 &= ~0x200000;
  asm volatile("mov %0, %%cr0" ::"r"(cr0));
  asm volatile("fninit");
}

static void isr8_handler(registers_t *regs) {
  (void)regs;
  serial_log("FATAL: DOUBLE FAULT!");
  for (;;)
    ;
}

static void isr9_handler(registers_t *regs) {
  (void)regs;
  serial_log("EXCEPTION: #NM (FPU not available)");
  enable_fpu();
}

extern "C" void __cxx_global_ctor_init();

extern "C" void init_brain_syscalls(void);

extern "C" int main() {
  init_serial();
  // Yahan se asli kahani shuru hoti hai
  serial_log("KERNEL: Booting Higher-Half Retro-OS...");

  extern char _bss_start, _bss_end, _kernel_end;
  serial_log_hex("KERNEL: BSS Start: ", (uintptr_t)&_bss_start);
  serial_log_hex("SMP: Param PDPT:    ", (uintptr_t)*(uint32_t*)0x1008);
  serial_log_hex("KERNEL: BSS End:   ", (uintptr_t)&_bss_end);
  serial_log_hex("KERNEL: End:       ", (uintptr_t)&_kernel_end);

  // 0. Early CPU Setup taaki faults pakad sakein
  serial_log("KERNEL: Init GDT...");
  init_gdt();
  serial_log("KERNEL: Init ISRs...");
  isr_install();
  pci_probe();

  // 1. Core Memory setup (Sabse pehle ye zaroori hai)
  init_memory(PHYS_TO_VIRT(PLACEMENT_PHYS_START)); 

  // Parse E820 memory map from bootloader
  serial_log("KERNEL: Parsing E820 memory map...");
  e820_parse(true); 
  e820_print_map();

  // 2. PMM initialization — now E820-aware!
  serial_log("KERNEL: Init PMM...");
  
  uint32_t low_mem_size = e820_get_usable_low_memory();
  if (low_mem_size == 0) {
      low_mem_size = TOTAL_RAM;
      serial_log("KERNEL: WARNING — E820 failed, using hardcoded TOTAL_RAM");
  }
  if (low_mem_size > 0xFFF00000) low_mem_size = 0xFFF00000;
  
  serial_log_hex("KERNEL: Low memory size for PMM: ", low_mem_size);
  
  uint32_t *bitmap_addr = (uint32_t *)PHYS_TO_VIRT(0x00E00000); // 14MB
  pmm_init(low_mem_size, bitmap_addr);

  // Use E820 regions instead of blanket free
  e820_init_pmm_regions();

  // -- Step B: Re-reserve regions that must never be allocated --------------
  pmm_mark_region_used(0x0,       0x100000); // Low 1 MB (IVT, BIOS, VGA, etc.)
  pmm_mark_region_used(KERNEL_PHYS_START,  0xA00000); // Kernel image
  pmm_mark_region_used(PLACEMENT_PHYS_START,  PLACEMENT_SIZE); // Placement heap
  pmm_mark_region_used(VIRT_TO_PHYS(bitmap_addr), 32768); // PMM bitmap (32KB for 1GB)
  
  // -- Step C: Reserve Ring 2 Territory -------------------------------------
  pmm_mark_region_used(RING2_PHYS_START, RING2_REGION_SIZE);

  // -- Step D: Reserve FULL Kernel Heap -------------------------------------
  // Heap: physical 16MB to 272MB (256MB total)
  // Contains: buddy allocator, slab allocator, GUI backbuffer, all kmalloc
  // WITHOUT this, PMM hands heap pages to user processes -> corruption
  pmm_mark_region_used(KHEAP_PHYS_START, KHEAP_SIZE);
  
  // Reserve E820 data area
  pmm_mark_region_used(E820_BOOT_COUNT_PHYS, 
                       E820_BOOT_DATA_PHYS + (E820_BOOT_MAX_ENTRIES * 24) 
                       - E820_BOOT_COUNT_PHYS);

  serial_log("KERNEL: PMM After Reservations:");
  pmm_print_stats();

  // Initialize high-memory allocator
  serial_log("KERNEL: Init High-Memory PMM...");
  pmm_high_init();

  // 3. Full PAE paging setup (Boot mapping se 3-level 64GB map ki taraf)
  serial_log("KERNEL: Init PAE 3-Level Paging...");
  pae_init(); 

  // --- High Memory Verification Loop (PAE Sliding Window Test) ---
  uint64_t high_phys = pmm_high_alloc(1); // Get 1x 2MB chunk above 4GB
  if (high_phys) {
      void* mapped = pae_map_window(high_phys, 0); // Window into high memory
      uint32_t* test_ptr = (uint32_t*)mapped;
      *test_ptr = 0xDEADC0DE; // Write to Physical RAM > 4GB
      
      serial_log_hex("PAE TEST: Wrote 0xDEADC0DE to high physical ", (uint32_t)high_phys);
      if (*test_ptr == 0xDEADC0DE) {
          serial_log("PAE TEST: Readback successful. High Memory Accessible! ✓");
      } else {
          serial_log("PAE TEST: ERROR - Readback mismatch. Paging Failure. ✗");
      }
      pae_unmap_window(0);
      pmm_high_free(high_phys, 1);
  }

  // 3.5 Zero Ring 2 stack memory
  // Must happen AFTER paging (so virtual addresses work)
  // Must happen BEFORE anything uses Ring 2
  serial_log("KERNEL: Zeroing Ring 2 stack...");
  memset((void*)RING2_STACK_VIRT, 0, RING2_STACK_SIZE);
  serial_log_hex("KERNEL: Ring 2 stack zeroed at ", RING2_STACK_VIRT);
  serial_log_hex("KERNEL: Ring 2 stack top (ESP) ", RING2_STACK_TOP_VIRT);

  // 4. Paging ke baad ki taiyari
  init_syscalls();
  init_brain_syscalls();
  void ring2_foundation_verify(void);
  ring2_foundation_verify();

  // 4. Activate PAE Security (NX Compensation)
  pae_security_init();

  register_interrupt_handler(8, (isr_t)isr8_handler);
  register_interrupt_handler(9, (isr_t)isr9_handler);

  serial_log("KERNEL: Interrupts & Syscalls & GDT Ready.");

  // 5. Hardware Interface initialization
  irq_install(); // ACPI / APIC / IO-APIC

  // Interrupts enable karne se pehle input initialize karo
  init_keyboard();
  init_mouse();

    // Global interrupts will be enabled later after all cores are online
    // asm volatile("sti");

  hpet_init();
  tsc_calibrate();
  serial_log("KERNEL: Drivers & Timers Active.");

  enable_fpu();

  // 6. Heap & Filesystem - 256MB heap (16MB to 272MB physical)
  // Note: init_paging maps 0-512MB physical. Heap must stay within this range.
  init_kheap(PHYS_TO_VIRT(KHEAP_PHYS_START), PHYS_TO_VIRT(KHEAP_PHYS_END),
             PHYS_TO_VIRT(KHEAP_PHYS_END));
  set_heap_status(1);
  slab_init();
  extern int slab_is_initialized;
  slab_is_initialized = 1;

  serial_log("KERNEL: Init Block Layer...");
  serial_log("KERNEL: Calling block_init()...\n");
  block_init();
  serial_log("KERNEL: block_init() done.\n");
  block_print_devices();

  ac97_init();
  sb16_init();
  // vfs_root = fat16_vfs_init(); // Handled by vfs_init
  // vfs_dev = devfs_init(); // Handled by vfs_init
  socket_init();
  tty_init();
  // disk_allocator_init(); // Removed legacy init
  // fs_init(); // Removed legacy init

  serial_log("KERNEL: Init VFS (Phase 4)...");
  vfs_init();

  serial_log("KERNEL: PMM After VFS:");
  pmm_print_stats();

  kernel_core_init();
  kernel_advanced_init();

  // 6.5 Network Initialization
  uint8_t n_bus, n_slot, n_func;
  if (pci_find_device(0x8086, 0x100E, &n_bus, &n_slot, &n_func)) {
    serial_log("KERNEL: Network Card (e1000) Found!");
    e1000_init(n_bus, n_slot, n_func);
  } else {
    serial_log("KERNEL: Network Card (e1000) NOT Found.");
  }

  // 7. Graphics & SMP Initialization
  serial_log("KERNEL: Scanning PCI for BGA...");
  uint32_t bga_phys = pci_get_bga_bar0(); // Physical BAR0
  uint32_t virtual_fb = 0xFD000000;       // Fixed virtual landing

  if (bga_phys == 0) {
    serial_log("KERNEL: FATAL - BGA Not Found! Attempting VBE fallback.");
  } else {
    // 6. APIC & SMP - Handled by irq_install()
    serial_log_hex("APIC: Verified Processors count: ", total_cpus);

    // 7. Graphics Initialization
    bga_set_video_mode(1024, 768, 32);
    
    serial_log_hex("KERNEL: BGA LFB Physical: ", bga_phys);
    serial_log("KERNEL: Mapping VRAM (16 MB) via range...");
    // 16MB = 4096 pages
    pae_map_range(virtual_fb, bga_phys, 4096, 
                  PAE_FLAG_PRESENT | PAE_FLAG_WRITABLE | PAE_FLAG_PCD | PAE_FLAG_PWT);

    init_graphics(virtual_fb);
    serial_log_hex("GRAPHICS: BGA Framebuffer mapped at: ", virtual_fb);

    // Initial Screen Clear (Blackout)
    for (uint32_t i = 0; i < 1024 * 768; i++) {
        ((uint32_t *)(uintptr_t)virtual_fb)[i] = 0;
    }

    // 8. Process & Scheduler Initialization
    // CRITICAL: Initialize multitasking structures BEFORE booting secondary cores
    init_multitasking(g_pdpt_phys_addr);

    // 9. Start Multi-Core SMP
    extern volatile uint32_t g_cores_online;
    g_cores_online = 0x1; // Mark BSP as online (Bit 0)

    serial_log("SMP: Starting secondary cores...\n");
    smp_init();

    // 10. MOBILIZATION GATE: Wait for all 4 cores OR at least Core 1 + Timeout
    serial_log("KERNEL: Waiting for SMP mobilization (Target: Core 1 ready)...");
    uint32_t last_mask = 0;
    uint32_t wait_count = 0;
    // Condition: Exit if all 4 online, OR if Core 1 is online and we've waited a bit
    while (g_cores_online != 0xF && wait_count < 20000000) {
        if (g_cores_online != last_mask) {
            serial_log_hex("\nKERNEL: Mobilization Bitmask Update: ", g_cores_online);
            last_mask = g_cores_online;
            
            // Optimization: If Core 1 (GUI) is online, we can potentially proceed sooner
            if ((g_cores_online & 0x03) == 0x03 && wait_count > 5000000) break;
        }
        wait_count++;
        asm volatile("" ::: "memory"); 
        asm volatile("pause");
    }

    if (g_cores_online == 0xF) {
        serial_log("KERNEL: 4-Core Mobilization Complete. Full System READY.");
        
        // Rev R: Interrupt Steering
        // Core 1 (Index 1) is GUI. Route Mouse (12) and Keyboard (1)
        uint8_t core1_apic = cpu_apic_ids[1];
        ioapic_route_irq(1, core1_apic);
        ioapic_route_irq(12, core1_apic);
        
        // Core 2 (Index 2) is Brain. Route AHCI (11)
        uint8_t core2_apic = cpu_apic_ids[2];
        ioapic_route_irq(11, core2_apic);
        
        serial_log("KERNEL: Interrupt Steering Active (GUI=Core1, AI=Core2, Sys=Core0)");
    } else {
        serial_log_hex("KERNEL: WARNING — Mobilization Timeout. Bitmask: ", g_cores_online);
    }

    // Spawn high-level threads only after all hardware is verified
    process_t* gui_proc = create_kernel_thread(gui_main);
    if(gui_proc) {
        gui_proc->pinned_cpu = 1;
        gui_proc->priority = 50;
    }
    serial_log("KERNEL: GUI thread spawned (CPU=1).\n");
    
    // Spawn Networking thread (Core 3)
    serial_log("KERNEL: Starting Net System...");
    process_t* net_proc = create_kernel_thread(net_thread);
    if(net_proc) {
        net_proc->pinned_cpu = 3;
        net_proc->priority = 50;
    }
    serial_log("KERNEL: Net thread spawned (CPU=3).\n");

    serial_log("KERNEL: Starting Scheduler...");
    init_timer(100); // Start scheduler at 100Hz
    
    // Ring 2 Brain Setup
    void brain_mailbox_init(void);
    brain_mailbox_init();

    // Spawn AI Brain thread (Core 2)
    serial_log("KERNEL: Spawning AI Brain thread...\n");
    process_t* brain_proc = create_kernel_thread((void(*)())launch_ring2);
    if(brain_proc) {
        brain_proc->priority = 50;
        brain_proc->pinned_cpu = 2;
    }
    serial_log("KERNEL: Brain thread spawned (CPU=2).\n");
    
    // 10. FINAL ACTION: Release APs and Enable Core 0 interrupts
    extern volatile uint32_t g_smp_ready;
    serial_log("KERNEL: Releasing secondary cores (g_smp_ready = 1)...");
    g_smp_ready = 1;

    // Small delay to ensure memory visibility and let APs exit parking
    for(volatile int i = 0; i < 2000000; i++) asm volatile("pause");

    serial_log("KERNEL: All Cores mobilized. Enabling global interrupts...");
    asm volatile("sti");

    // Load INIT.ELF
    serial_log("KERNEL: Loading /C/INIT.ELF...");
    if (create_user_process("/C/INIT.ELF", nullptr) < 0) {
        serial_log("KERNEL: /C/INIT.ELF not found, trying /D/INIT.ELF...");
        create_user_process("/D/INIT.ELF", nullptr);
    }
    
    serial_log("KERNEL: Higher-Half Kernel Running.");
  }

  while (1) {
    asm volatile("hlt");
  }

  return 0;
}