#include "../drivers/serial.h"
#include "../include/signal.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "paging.h"
#include "pipe.h"
#include "pmm.h"
#include "process.h"
#include "socket.h"
#include "vm.h"

// External FAT16 function
extern "C" int fat16_create_file(const char *filename);
extern "C" void fat16_get_stats_bytes(uint32_t *total_bytes,
                                      uint32_t *free_bytes);

int sys_chdir(const char *path) {
  vfs_node_t *node = vfs_resolve_path(path);
  if (node && (node->flags & 0x7) == VFS_DIRECTORY) {
    if (path[0] == '/') {
      strcpy(current_process->cwd, path);
    } else {
      // Append to current CWD (simplified)
      strcat(current_process->cwd, "/");
      strcat(current_process->cwd, path);
    }
    return 0;
  }
  return -1;
}

int sys_mkdir(const char *path) { return mkdir_vfs(vfs_root, path, 0); }

int sys_unlink(const char *path) { return unlink_vfs(vfs_root, path); }

int sys_getcwd(char *buf, uint32_t size) {
  if (!buf || size < strlen(current_process->cwd) + 1)
    return -1;
  strcpy(buf, current_process->cwd);
  return 0;
}

static void syscall_handler(registers_t *regs);

void init_syscalls() { register_interrupt_handler(0x80, syscall_handler); }

void syscall_handler(registers_t *regs) {
  // The syscall number is in EAX
  // Arguments in EBX, ECX, EDX, ESI, EDI
  serial_log_hex("SYSCALL: Called ID ", regs->eax);

  if (regs->eax == 0) { // Call ID 0: print_serial
    const char *str = (const char *)(uintptr_t)regs->ebx;
    serial_log_hex("  String at: ", (uint32_t)(uintptr_t)str);
    serial_log(str);
  } else if (regs->eax == 1) { // Call ID 1: get_pid
    regs->eax = current_process->id;
  } else if (regs->eax == 2) { // Call ID 2: open
    char *path = (char *)(uintptr_t)regs->ebx;
    uint32_t flags = regs->ecx;

    // Path walking - handles paths like "/dev/null" or "FILE.TXT"
    vfs_node_t *node = vfs_root;
    char name[128];
    int len = strlen(path);
    int i = 0;
    char *last_name = 0; // Track last component for O_CREAT

    if (path[0] != '/') {
      // Relative path - start from CWD
      // For now, if we're in root, it's the same.
      // A proper implementation would walk the CWD path.
      // But since we only have one filesystem, we can just prepend CWD if
      // needed.
    }

    while (i < len && node) {
      // Skip leading slashes
      while (i < len && path[i] == '/')
        i++;
      if (i >= len)
        break;

      // Extract component name
      int start = i;
      while (i < len && path[i] != '/')
        i++;
      int clen = i - start;
      if (clen > 127)
        clen = 127;
      memcpy(name, path + start, clen);
      name[clen] = 0;
      last_name = name;

      // Find child
      vfs_node_t *child = finddir_vfs(node, name);
      if (!child) {
        // Check if O_CREAT flag is set and this is the last component
        int remaining = i;
        while (remaining < len && path[remaining] == '/')
          remaining++;
        if ((flags & O_CREAT) && remaining >= len) {
          // Create the file
          fat16_create_file(name);
          // Try again
          child = finddir_vfs(node, name);
        }
        if (!child) {
          node = 0;
          break;
        }
      }
      node = child;
    }

    if (node) {
      for (int j = 0; j < MAX_PROCESS_FILES; j++) {
        if (current_process->fd_table[j] == 0) {
          current_process->fd_table[j] = node;
          regs->eax = j;
          return;
        }
      }
    }
    regs->eax = -1;
  } else if (regs->eax == 3) { // Call ID 3: read
    int fd = (int)regs->ebx;
    uint8_t *buf = (uint8_t *)regs->ecx;
    uint32_t size = (uint32_t)regs->edx;
    if (fd >= 0 && fd < MAX_PROCESS_FILES && current_process->fd_table[fd]) {
      regs->eax = read_vfs(current_process->fd_table[fd], 0, size, buf);
    } else {
      regs->eax = -1;
    }
  } else if (regs->eax == 4) { // Call ID 4: write
    int fd = (int)regs->ebx;
    uint8_t *buf = (uint8_t *)regs->ecx;
    uint32_t size = (uint32_t)regs->edx;
    if (fd >= 0 && fd < MAX_PROCESS_FILES && current_process->fd_table[fd]) {
      regs->eax = write_vfs(current_process->fd_table[fd], 0, size, buf);
    } else {
      regs->eax = -1;
    }
  } else if (regs->eax == 5) { // Call ID 5: close
    int fd = (int)regs->ebx;
    if (fd >= 0 && fd < MAX_PROCESS_FILES && current_process->fd_table[fd]) {
      close_vfs(current_process->fd_table[fd]);
      current_process->fd_table[fd] = 0;
      regs->eax = 0;
    } else {
      regs->eax = -1;
    }
  } else if (regs->eax == 6) { // Call ID 6: sbrk
    intptr_t increment = (intptr_t)regs->ebx;
    uint32_t old_brk = current_process->heap_end;
    uint32_t new_brk = old_brk + increment;

    // TODO: Check for overflow or limits

    // Check if we need to allocate more pages
    uint32_t old_page_top = (old_brk + 0xFFF) & 0xFFFFF000;
    uint32_t new_page_top = (new_brk + 0xFFF) & 0xFFFFF000;

    if (new_page_top > old_page_top) {
      // We need to allocate pages
      for (uint32_t page = old_page_top; page < new_page_top; page += 4096) {
        // Verify it's not already mapped?
        // vm_map_page requires us to switch context to current_process's PD if
        // not active? Syscalls happen in kernel context, but CR3 should still
        // be the User Process's PD (mostly) WAIT: When interrupt happens, CR3
        // is NOT changed. So we are in the Current Process context.

        void *phys = pmm_alloc_block();
        if (!phys) {
          regs->eax = -1; // OOM
          return;
        }
        vm_map_page((uint32_t)phys, page, 7); // User|RW|Present
        memset((void *)page, 0, 4096); // Zero out new memory for security
      }
    } else if (new_page_top < old_page_top) {
      // Freeing memory (shrink heap)
      // Not implemented fully yet (needs pmm_free_block)
      // For now, we update brk but don't unmap to avoid complex logic
    }

    current_process->heap_end = new_brk;
    regs->eax = old_brk;
  } else if (regs->eax == 7) { // Call ID 7: mmap
    uint32_t addr = regs->ebx;
    uint32_t length = regs->ecx;
    // For now we ignore prot and flags and just allocate anonymous memory

    if (addr == 0) {
      // Simple bump allocator for mmap
      static uint32_t next_mmap_addr = 0x80000000;
      addr = next_mmap_addr;
      next_mmap_addr += (length + 0xFFF) & 0xFFFFF000;
    }

    uint32_t num_pages = (length + 0xFFF) / 4096;
    for (uint32_t i = 0; i < num_pages; i++) {
      uint32_t virt = addr + i * 4096;
      void *phys = pmm_alloc_block();
      if (!phys) {
        regs->eax = 0;
        return;
      }
      vm_map_page((uint32_t)phys, virt, 7); // User|RW|Present
      memset((void *)virt, 0, 4096);
    }
    regs->eax = addr;
  } else if (regs->eax == 8) { // Call ID 8: munmap
    uint32_t addr = regs->ebx;
    uint32_t length = regs->ecx;
    uint32_t num_pages = (length + 0xFFF) / 4096;
    for (uint32_t i = 0; i < num_pages; i++) {
      vm_unmap_page(addr + i * 4096);
    }
    regs->eax = 0;
  } else if (regs->eax == 9) { // Call ID 9: fork
    regs->eax = fork_process(regs);
  } else if (regs->eax == 10) { // Call ID 10: execve
    const char *path = (const char *)regs->ebx;
    char **argv = (char **)regs->ecx;
    char **envp = (char **)regs->edx;
    regs->eax = exec_process(regs, path, argv, envp);
  } else if (regs->eax == 11) { // Call ID 11: wait
    int *status = (int *)regs->ebx;
    regs->eax = wait_process(status);
  } else if (regs->eax == 12) { // Call ID 12: exit
    exit_process(regs->ebx);
  } else if (regs->eax == 13) { // Call ID 13: pipe
    uint32_t *filedes = (uint32_t *)regs->ebx;
    regs->eax = sys_pipe(filedes);
  } else if (regs->eax == 14) { // Call ID 14: socket
    regs->eax = sys_socket(regs->ebx, regs->ecx, regs->edx);
  } else if (regs->eax == 15) { // Call ID 15: bind
    regs->eax = sys_bind(regs->ebx, (const char *)regs->ecx);
  } else if (regs->eax == 16) { // Call ID 16: connect
    regs->eax = sys_connect(regs->ebx, (const char *)regs->ecx);
  } else if (regs->eax == 17) { // Call ID 17: accept
    regs->eax = sys_accept(regs->ebx);
  } else if (regs->eax == 18) { // Call ID 18: signal
    regs->eax = sys_signal(regs->ebx, (sighandler_t)regs->ecx);
  } else if (regs->eax == 19) { // Call ID 19: kill
    regs->eax = sys_kill(regs->ebx, regs->ecx);
  } else if (regs->eax == 20) { // Call ID 20: sigreturn
    regs->eax = kernel_sigreturn(regs);
  } else if (regs->eax == 21) { // Call ID 21: readdir
    int fd = (int)regs->ebx;
    uint32_t index = (uint32_t)regs->ecx;
    struct dirent *de = (struct dirent *)regs->edx;
    if (fd >= 0 && fd < MAX_PROCESS_FILES && current_process->fd_table[fd]) {
      struct dirent *res = readdir_vfs(current_process->fd_table[fd], index);
      if (res) {
        memcpy(de, res, sizeof(struct dirent));
        regs->eax = 0;
      } else {
        regs->eax = -1;
      }
    } else {
      regs->eax = -2;
    }
  } else if (regs->eax == 22) { // Call ID 22: statfs
    uint32_t *total = (uint32_t *)regs->ebx;
    uint32_t *free = (uint32_t *)regs->ecx;
    uint32_t *block_size = (uint32_t *)regs->edx;
    if (block_size)
      *block_size = 1; // Return in bytes
    fat16_get_stats_bytes(total, free);
    regs->eax = 0;
  } else if (regs->eax == 23) { // Call ID 23: chdir
    regs->eax = sys_chdir((const char *)regs->ebx);
  } else if (regs->eax == 24) { // Call ID 24: unlink
    regs->eax = sys_unlink((const char *)regs->ebx);
  } else if (regs->eax == 25) { // Call ID 25: mkdir
    regs->eax = sys_mkdir((const char *)regs->ebx);
  } else if (regs->eax == 26) { // Call ID 26: getcwd
    regs->eax = sys_getcwd((char *)regs->ebx, (uint32_t)regs->ecx);
  }
}
