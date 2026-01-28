// Contracts aur Interfaces uthao shuru mein
#include "../include/Contracts.h"
#include "../include/KernelInterfaces.h"
#include <fs_phase.h>

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
#include "slab.h"
#include "socket.h"
#include "syscall.h"
#include "tsc.h"
#include "tty.h"

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

extern "C" uint32_t tick;
extern "C" uint32_t sys_time_ms() {
  return tick * 20; // 50Hz = 20ms per tick
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
  create_user_process(path, argv);
  return 0;
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

static vfs_node_t *k_fd_table[32];

// Helper to resolve handle to node
static vfs_node_t *resolve_k_fd(int fd) {
  if (fd >= 1000 && fd < 1032)
    return k_fd_table[fd - 1000];
  if (fd > 0x100000)
    return (vfs_node_t *)fd; // Direct pointer
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
  }
  return 0;
}

extern "C" int sys_read(int fd, void *buf, uint32_t len) {
  vfs_node_t *node = resolve_k_fd(fd);
  if (!node)
    return -1;
  return vfs_read(node, 0, buf, len);
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

extern "C" int main() {
  init_serial();
  // Yahan se asli kahani shuru hoti hai
  serial_log("KERNEL: Booting Higher-Half Retro-OS...");

  extern char _bss_start, _bss_end, _kernel_end;
  serial_log_hex("KERNEL: BSS Start: ", (uint32_t)&_bss_start);
  serial_log_hex("KERNEL: BSS End:   ", (uint32_t)&_bss_end);
  serial_log_hex("KERNEL: End:       ", (uint32_t)&_kernel_end);

  // 0. Early CPU Setup taaki faults pakad sakein
  serial_log("KERNEL: Init GDT...");
  init_gdt();
  serial_log("KERNEL: Init ISRs...");
  isr_install();

  // 1. Core Memory setup (Sabse pehle ye zaroori hai)
  // Kernel ends at about 6.2MB (_kernel_end = 0xC062F000 = physical 0x0062F000)
  // Placement heap at 7MB - safely beyond kernel
  init_memory(PHYS_TO_VIRT(0x00700000)); // 7MB placement heap

  serial_log("KERNEL: Init PMM at 8MB...");
  uint32_t mem_size = 512 * 1024 * 1024;
  // PMM bitmap at 8MB - safely beyond kernel end (6.2MB) and placement heap
  // (7MB)
  uint32_t *bitmap_addr = (uint32_t *)PHYS_TO_VIRT(0x00800000); // 8MB
  pmm_init(mem_size, bitmap_addr);

  pmm_mark_region_used(0x0, 0x100000);      // Low Memory (0-1MB)
  pmm_mark_region_used(0x100000, 0x700000); // Kernel + Placement Heap (1-8MB)
  pmm_mark_region_used(VIRT_TO_PHYS(bitmap_addr), 16384); // PMM Bitmap at 8MB
  // pmm_mark_region_used(0x01000000, 0x20000000); // Kernel Heap Physical
  // (512MB) - Don't mark used!

  serial_log("KERNEL: PMM After Reservations:");
  pmm_print_stats();

  // 3. Full paging setup (Boot mapping se unified map ki taraf)
  serial_log("KERNEL: Init Paging...");
  init_paging();

  // 4. Paging ke baad ki taiyari
  init_syscalls();
  register_interrupt_handler(8, (isr_t)isr8_handler);
  register_interrupt_handler(9, (isr_t)isr9_handler);

  serial_log("KERNEL: Interrupts & Syscalls & GDT Ready.");

  // 5. Hardware Interface initialization
  irq_install(); // ACPI / APIC / IO-APIC

  // Interrupts enable karne se pehle input initialize karo
  init_keyboard();
  init_mouse();

  serial_log("KERNEL: Enabling global interrupts...");
  asm volatile("sti");

  hpet_init();
  tsc_calibrate();
  serial_log("KERNEL: Drivers & Timers Active.");

  enable_fpu();

  // 6. Heap & Filesystem - 256MB heap (16MB to 272MB physical)
  // Note: init_paging maps 0-512MB physical. Heap must stay within this range.
  // Using 256MB to leave room for kernel, page tables, etc.
  init_kheap(PHYS_TO_VIRT(0x01000000), PHYS_TO_VIRT(0x11000000),
             PHYS_TO_VIRT(0x11000000));
  set_heap_status(1);
  slab_init();
  extern int slab_is_initialized;
  slab_is_initialized = 1;

  // C++ global constructors initialize karo (vtables ke liye zaroori hai)
  __cxx_global_ctor_init();

  fat16_init();
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

  // 7. Graphics (BGA)
  serial_log("KERNEL: Scanning PCI for BGA...");
  u32 fb_addr = pci_get_bga_bar0();

  if (fb_addr == 0) {
    serial_log("KERNEL: FATAL - BGA Not Found! Attempting VBE fallback.");
    // TODO: agar zarurat padi toh VBE fallback lagayenge
  } else {
    serial_log_hex("KERNEL: BGA LFB Address: ", fb_addr);
    // KERNEL LFB
    bga_set_video_mode(1024, 768, 32);

    // Ek bada framebuffer region map karo (e.g.,q7 16 MB) safety ke liye
    // Dhyan rahe ye mapping init_paging() ke BAAD honi chahiye
    const uint32_t fb_size = 16 * 1024 * 1024; // 16 MB
    const uint32_t pages = fb_size / 4096;
    serial_log("KERNEL: Mapping VRAM (16 MB)...");
    for (uint32_t i = 0; i < pages; i++) {
      paging_map(fb_addr + (i * 4096), fb_addr + (i * 4096), 3);
    }

    serial_log("KERNEL: PMM After VRAM Map:");
    pmm_print_stats();

    init_graphics(fb_addr);
    // Saaf saaf black kar do
    for (uint32_t i = 0; i < 1024 * 768; i++) {
      ((uint32_t *)fb_addr)[i] = 0x0;
    }

    // 8. User Space ki shuruat
    init_multitasking();

    // gui_main ko kernel thread ki tarah start karo
    serial_log("KERNEL: Starting GUI System...");
    create_kernel_thread(gui_main);

    // Networking thread
    serial_log("KERNEL: Starting Net System...");
    create_kernel_thread(net_thread);

    // User space start karo - Non-GUI INIT chala rahe hain
    create_user_process("INIT.ELF", nullptr);
    init_timer(50);
    serial_log("KERNEL: Higher-Half Kernel Running.");
  }

  while (1) {
    asm volatile("hlt");
  }
  return 0;
}