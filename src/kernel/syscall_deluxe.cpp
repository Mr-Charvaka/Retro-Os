#include "syscall.h"
#include "../drivers/rtc.h"
#include "../drivers/serial.h"
#include "../include/errno.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "heap.h"
#include "memory.h"
#include "pmm.h"
#include "process.h"
#include "tty.h"
#include "vm.h"

extern "C" {

// Kernel-side POSIX constants
#define S_IFDIR 0x4000
#define S_IFREG 0x8000

struct kernel_stat {
  uint32_t st_dev, st_ino, st_mode, st_nlink, st_uid, st_gid, st_rdev, st_size;
  uint32_t st_atime, st_mtime, st_ctime, st_blksize, st_blocks;
};

// --- Improved POSIX Syscall Bridge ---

int sys_stat_impl(registers_t *regs) {
    const char *path = (const char *)regs->ebx;
    struct kernel_stat *st = (struct kernel_stat *)regs->ecx;
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) return -ENOENT;
    
    memset(st, 0, sizeof(struct kernel_stat));
    st->st_ino = node->inode;
    st->st_size = node->size;
    st->st_mode = (node->flags & 0x07); 
    if ((node->flags & 0x7) == VFS_DIRECTORY) st->st_mode = S_IFDIR | 0777;
    else st->st_mode = S_IFREG | 0777;
    
    rtc_time_t now; rtc_read(&now);
    st->st_mtime = now.hour * 3600 + now.minute * 60 + now.second;
    st->st_blksize = 512;
    return 0;
}

int sys_fstat_impl(registers_t *regs) {
    int fd = (int)regs->ebx;
    struct kernel_stat *st = (struct kernel_stat *)regs->ecx;
    if (fd < 0 || fd >= MAX_PROCESS_FILES || !current_process->fd_table[fd]) return -EBADF;
    
    vfs_node_t *node = current_process->fd_table[fd]->node;
    memset(st, 0, sizeof(struct kernel_stat));
    st->st_size = node->size;
    st->st_mode = (node->flags & 0x07) == VFS_DIRECTORY ? S_IFDIR|0777 : S_IFREG|0777;
    return 0;
}

extern "C" int vfs_mkdir(const char *path, uint32_t mode);
extern "C" int vfs_rmdir(const char *path);

int sys_mkdir_impl(registers_t *regs) {
    const char *path = (const char *)regs->ebx;
    return vfs_mkdir(path, 0755);
}

int sys_rmdir_impl(registers_t *regs) {
    const char *path = (const char *)regs->ebx;
    return vfs_rmdir(path);
}

void syscall_deluxe_init() {
    // This will be called from Kernel.cpp or via manual assignment in syscall.cpp
}

} // extern "C"
