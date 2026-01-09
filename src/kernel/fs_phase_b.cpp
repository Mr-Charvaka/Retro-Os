/*
 * fs_phase_b.cpp
 *
 * PHASE B: Kernel Syscall Layer + User Utilities
 */

#include <fs_phase.h>
#include <string.h>

extern "C" void serial_log(const char *msg);

// Helper for integer printing
extern "C" void serial_log_int(int val) {
  char buf[32];
  extern char *itoa(int num, char *str, int base);
  itoa(val, buf, 10);
  serial_log(buf);
}

// 🔹 SECTION 1: GLOBAL PROCESS CONTEXT (PWD FIX)
static char current_working_dir[MAX_PATH] = "/";
static uint16_t current_uid = 1000;

// 🔹 SECTION 2: cd IMPLEMENTATION (REAL)
extern "C" bool cmd_cd(const char *path) {
  phase_inode *dir = phase_vfs_resolve(path);
  if (!dir || dir->type != INODE_DIR) {
    serial_log("cd: no such directory");
    return false;
  }
  strcpy(current_working_dir, path);
  return true;
}

// 🔹 SECTION 3: pwd
extern "C" void cmd_pwd() { serial_log(current_working_dir); }

// 🔹 SECTION 4: ls — DIRECTORY LISTING (CORE FIX)
extern "C" void cmd_ls(const char *path) {
  const char *p = path ? path : current_working_dir;
  phase_inode *dir = phase_vfs_resolve(p);

  if (!dir || dir->type != INODE_DIR) {
    serial_log("ls: invalid directory");
    return;
  }

  phase_dir_entry *ents = (phase_dir_entry *)phase_data_blocks[dir->blocks[0]];
  for (uint32_t i = 0; i < dir->size; i++) {
    serial_log(ents[i].name);
  }
}

// 🔹 SECTION 5: stat — METADATA VIEWER
extern "C" void cmd_stat(const char *path) {
  phase_inode *f = phase_vfs_resolve(path);
  if (!f) {
    serial_log("stat: file not found");
    return;
  }

  serial_log("Inode: ");
  serial_log_int(f->id);
  serial_log("Type: ");
  serial_log(f->type == INODE_DIR ? "DIR" : "FILE");
  serial_log("Size: ");
  serial_log_int(f->size);
  serial_log("Owner: ");
  serial_log_int(f->owner);
  serial_log("Perms: ");
  serial_log_int(f->perms);
}

// 🔹 SECTION 6: SYS_READ / SYS_LS / SYS_STAT (GUI BRIDGE)

extern "C" int sys_ls_direct(const char *path, phase_dir_entry *out, int max) {
  phase_inode *dir = phase_vfs_resolve(path);
  if (!dir || dir->type != INODE_DIR)
    return -1;

  phase_dir_entry *src = (phase_dir_entry *)phase_data_blocks[dir->blocks[0]];
  int count = dir->size < max ? dir->size : max;

  for (int i = 0; i < count; i++)
    out[i] = src[i];

  return count;
}

extern "C" int sys_stat_direct(const char *path, phase_inode *out) {
  phase_inode *f = phase_vfs_resolve(path);
  if (!f)
    return -1;
  *out = *f;
  return 0;
}

extern "C" int sys_read_direct(const char *path, char *buf, int max) {
  phase_inode *f = phase_vfs_resolve(path);
  if (!f || f->type != INODE_FILE)
    return -1;

  int n = f->size < max ? f->size : max;
  memcpy(buf, phase_data_blocks[f->blocks[0]], n);
  return n;
}

// 🔹 SECTION 7: FILE EXPLORER OPEN LOGIC (DOUBLE-CLICK FIX)
extern "C" void explorer_open_direct(const char *full_path) {
  phase_inode meta;
  if (sys_stat_direct(full_path, &meta) != 0)
    return;

  if (meta.type == INODE_DIR) {
    cmd_cd(full_path);
  } else {
    char buf[1024];
    int n = sys_read_direct(full_path, buf, 1024);
    if (n > 0) {
      buf[n] = 0;
      serial_log(buf);
    }
  }
}
