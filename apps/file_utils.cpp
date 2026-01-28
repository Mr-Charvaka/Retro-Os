// file_utils.cpp
// Core File & Directory Utilities for Retro Pixel OS
// Adapted for Retro-OS Environment

#include "include/syscall.h"
#include "include/userlib.h"
#include <stdarg.h>

// ===== Kernel Contracts (Adapted for local usage) =====

struct Stat {
  uint32_t inode;
  uint32_t size;
  uint16_t mode; // permissions
  uint16_t type; // file / dir
};

struct DirEntry {
  char name[256];
  uint32_t inode;
  uint8_t type; // file or dir
};

// Simplified printf for file_utils
extern "C" void printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  char buf[1024];
  char *p = buf;

  while (*fmt) {
    if (*fmt == '%' && *(fmt + 1)) {
      fmt++;
      if (*fmt == 's') {
        const char *s = va_arg(args, const char *);
        while (*s)
          *p++ = *s++;
      } else if (*fmt == 'u' || *fmt == 'd') {
        unsigned int u = va_arg(args, unsigned int);
        char tmp[16];
        utoa(u, tmp, 10);
        char *t = tmp;
        while (*t)
          *p++ = *t++;
      } else if (*fmt == 'o') {
        unsigned int u = va_arg(args, unsigned int);
        char tmp[16];
        utoa(u, tmp, 8);
        char *t = tmp;
        while (*t)
          *p++ = *t++;
      } else if (*fmt == 'x') {
        unsigned int u = va_arg(args, unsigned int);
        char tmp[16];
        utoa(u, tmp, 16);
        char *t = tmp;
        while (*t)
          *p++ = *t++;
      } else {
        *p++ = *fmt;
      }
    } else {
      *p++ = *fmt;
    }
    fmt++;
  }
  *p = 0;
  syscall_print(buf);
  va_end(args);
}

// Wrapper translation to Kernel Syscalls
extern "C" {
int sys_open(const char *path, int flags) { return syscall_open(path, flags); }

int sys_close(int fd) { return syscall_close(fd); }

int sys_read(int fd, void *buf, size_t size) {
  return syscall_read(fd, buf, size);
}

int sys_write(int fd, const void *buf, size_t size) {
  return syscall_write(fd, buf, size);
}

int sys_mkdir(const char *path) { return syscall_mkdir(path, 0755); }

int sys_rmdir(const char *path) { return syscall_rmdir(path); }

int sys_unlink(const char *path) { return syscall_unlink(path); }

int sys_rename(const char *oldp, const char *newp) {
  return syscall_rename(oldp, newp);
}

int sys_stat(const char *path, Stat *out) {
  struct stat st;
  int r = syscall_stat(path, &st);
  if (r == 0) {
    out->inode = st.st_ino;
    out->size = st.st_size;
    out->mode = st.st_mode;
    out->type =
        (st.st_mode & 0x4000) ? 1 : 2; // Simple dir=1 logic for user's DirEntry
  }
  return r;
}

int sys_readdir(const char *path, DirEntry *out, int max) {
  int fd = syscall_open(path, 0);
  if (fd < 0)
    return 0;

  int count = 0;
  for (int i = 0; i < max; i++) {
    struct dirent de;
    if (syscall_readdir(fd, i, &de) != 0)
      break;

    if (de.d_name[0] == '.' &&
        (de.d_name[1] == 0 || (de.d_name[1] == '.' && de.d_name[2] == 0)))
      continue;

    strcpy(out[count].name, de.d_name);
    out[count].inode = de.d_ino;
    out[count].type =
        (de.d_type == 2) ? 1 : 2; // DIR=1, FILE=2 to match user's logic
    count++;
  }

  syscall_close(fd);
  return count;
}

int sys_getcwd(char *buf, size_t size) { return syscall_getcwd(buf, size); }

int sys_chdir(const char *path) { return syscall_chdir(path); }
}

// ===== Utilities (As requested) =====

// pwd
void cmd_pwd() {
  char buf[512];
  if (sys_getcwd(buf, sizeof(buf)) == 0)
    printf("%s\n", buf);
}

// cd
int cmd_cd(const char *path) { return sys_chdir(path); }

// mkdir
int cmd_mkdir(const char *path) { return sys_mkdir(path); }

// rmdir
int cmd_rmdir(const char *path) { return sys_rmdir(path); }

// rm
int cmd_rm(const char *path) { return sys_unlink(path); }

// mv
int cmd_mv(const char *src, const char *dst) { return sys_rename(src, dst); }

// stat
void cmd_stat(const char *path) {
  Stat st;
  if (sys_stat(path, &st) != 0) {
    printf("stat: failed\n");
    return;
  }

  printf("Inode: %u\n", st.inode);
  printf("Size: %u bytes\n", st.size);
  printf("Type: %s\n", st.type == 1 ? "Directory" : "File");
  printf("Mode: %o\n", st.mode);
}

// ls
void cmd_ls(const char *path) {
  DirEntry entries[256];
  int count = sys_readdir(path, entries, 256);

  for (int i = 0; i < count; i++) {
    printf("%s%s\n", entries[i].name, entries[i].type == 1 ? "/" : "");
  }
}

// cp
int cmd_cp(const char *src, const char *dst) {
  int in = sys_open(src, 0);
  int out = sys_open(dst, 1);
  if (in < 0 || out < 0)
    return -1;

  char buf[4096];
  int r;
  while ((r = sys_read(in, buf, sizeof(buf))) > 0) {
    sys_write(out, buf, r);
  }

  sys_close(in);
  sys_close(out);
  return 0;
}

// tree helper
void tree_recursive(const char *path, int depth) {
  DirEntry entries[256];
  int count = sys_readdir(path, entries, 256);

  for (int i = 0; i < count; i++) {
    for (int d = 0; d < depth; d++)
      printf("│   ");

    printf("├── %s\n", entries[i].name);

    if (entries[i].type == 1) {
      char next[512];
      // simplified snprintf Replacement
      strcpy(next, path);
      strcat(next, "/");
      strcat(next, entries[i].name);
      tree_recursive(next, depth + 1);
    }
  }
}

// tree
void cmd_tree(const char *path) {
  printf("%s\n", path);
  tree_recursive(path, 0);
}

// Dummy _start for app
extern "C" void _start() {
  printf("Retro Pixel OS File Utilities v1.0\n");
  printf("Current Directory: ");
  cmd_pwd();

  printf("\nListing root:\n");
  cmd_ls("/");

  syscall_exit(0);
}
