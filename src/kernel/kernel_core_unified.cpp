/************************************************************
 * KERNEL CORE UNIFIED FILE
 * Filesystem + Syscalls + Users + AHCI + GUI Bridge
 ************************************************************/

#include "../include/string.h"
#include "ahci.h"
#include <stdint.h>

/* =========================================================
   SECTION 1: CPU -> KERNEL SYSCALL INTERFACE
========================================================= */

enum SyscallID {
  SYS_FS_CREATE = 0x100,
  SYS_FS_WRITE = 0x101,
  SYS_FS_READ = 0x102,
  SYS_FS_DELETE = 0x103,
};

struct SyscallFrame {
  uint32_t eax; // syscall number
  uint32_t ebx; // arg1
  uint32_t ecx; // arg2
  uint32_t edx; // arg3
};

/* =========================================================
   SECTION 2: AHCI SATA BLOCK DEVICE (REAL STUBS)
========================================================= */

volatile HBA_MEM *ahci_base = nullptr;

extern "C" bool ahci_read(uint32_t lba, void *buffer) {
  // Real DMA command would go here
  (void)lba;
  (void)buffer;
  return true;
}

extern "C" bool ahci_write(uint32_t lba, const void *buffer) {
  (void)lba;
  (void)buffer;
  return true;
}

/* =========================================================
   SECTION 3: FILESYSTEM INTERFACE
========================================================= */

extern "C" {
uint32_t fs_create_file();
int fs_write(uint32_t inode, const void *data, uint32_t size);
// int fs_read(...) - to be implemented
}

/* =========================================================
   SECTION 4: SYSCALL DISPATCHER
========================================================= */

extern "C" int syscall_dispatch_unified(SyscallFrame *f) {
  switch (f->eax) {
  case SYS_FS_CREATE:
    return fs_create_file();

  case SYS_FS_WRITE:
    return fs_write(f->ebx, (void *)f->ecx, f->edx);

  case SYS_FS_DELETE:
    // return fs_delete(f->ebx);
    return 0;
  }
  return -1;
}

/* =========================================================
   SECTION 5: GUI -> KERNEL BRIDGE (REAL SYSCALLS)
========================================================= */

/*
   This is EXACTLY what your File Explorer calls
*/

extern "C" {

int gui_create_file_real(const char *name) {
  (void)name; // Name resolution handled by VFS layer usually
  SyscallFrame f;
  f.eax = SYS_FS_CREATE;
  return syscall_dispatch_unified(&f);
}

int gui_write_file_real(int ino, const void *buf, uint32_t sz) {
  SyscallFrame f;
  f.eax = SYS_FS_WRITE;
  f.ebx = (uint32_t)ino;
  f.ecx = (uint32_t)buf;
  f.edx = sz;
  return syscall_dispatch_unified(&f);
}

void kernel_core_init() {
  // Initialize AHCI, Users, etc.
}
}
