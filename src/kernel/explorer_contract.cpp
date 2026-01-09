// explorer_contract.cpp
// HARD CONTRACT between File Explorer GUI and Kernel VFS
// 32-bit, absolute-path enforced, crash-safe logic

#include "../drivers/serial.h"
#include "../include/string.h"
#include <stdint.h>

#define MAX_ITEMS 128
#define MAX_PATH 256

#define TYPE_FILE 1
#define TYPE_DIR 2

extern "C" {
// ===== Kernel Syscalls =====
int sys_open(const char *path, int flags);
int sys_close(int fd);
int sys_read(int fd, void *buf, uint32_t sz);
int sys_readdir(int fd, int index, void *out);
int sys_mkdir(const char *path, int perms);
int sys_stat(const char *path, void *stat_out);
int sys_spawn(const char *path, char **argv);
}

// =======================================================
// EXPLORER DATA MODEL (NO FAKE DATA ALLOWED)
// =======================================================

struct ExplorerItem {
  char name[64];
  char full_path[MAX_PATH]; // ABSOLUTE PATH
  uint32_t type;
};

ExplorerItem items[MAX_ITEMS];
int item_count = 0;
char cwd[MAX_PATH] = "/home/user/Desktop";

// =======================================================
// PATH UTILITIES
// =======================================================

static void normalize_path(char *path) {
  int len = strlen(path);
  if (len > 1 && path[len - 1] == '/')
    path[len - 1] = 0;
}

static void build_child_path(char *out, const char *parent, const char *name) {
  strcpy(out, parent);
  int len = strlen(out);
  if (len > 0 && out[len - 1] != '/')
    strcat(out, "/");
  strcat(out, name);
  normalize_path(out);
}

// =======================================================
// DIRECTORY LOADING (TRUTH SOURCE)
// =======================================================

extern "C" void explorer_load_directory(const char *path) {
  serial_log("Explorer: Loading directory...");
  serial_log(path);

  item_count = 0;
  strcpy(cwd, path);

  int fd = sys_open(path, 0);
  if (fd < 0) { // sys_open returns negative on failure, 0 is a valid FD
    serial_log("Explorer ERROR: cannot open directory");
    return;
  }

  for (int i = 0; i < MAX_ITEMS; i++) {
    struct {
      char name[64];
      uint32_t type;
    } ent;

    if (sys_readdir(fd, i, &ent) != 0)
      break;

    if (!ent.name[0] || strcmp(ent.name, ".") == 0 ||
        strcmp(ent.name, "..") == 0)
      continue;

    ExplorerItem &it = items[item_count++];
    strcpy(it.name, ent.name);
    it.type = ent.type;
    build_child_path(it.full_path, path, ent.name);
  }

  sys_close(fd);
}

// =======================================================
// OPEN CONTRACT (DOUBLE CLICK + RIGHT CLICK)
// =======================================================

extern "C" void explorer_open_item(int index) {
  if (index < 0 || index >= item_count)
    return;

  ExplorerItem &it = items[index];
  serial_log("Explorer OPEN: ");
  serial_log(it.full_path);

  if (it.type == TYPE_DIR) {
    explorer_load_directory(it.full_path);
  } else {
    // Check extension
    int len = strlen(it.full_path);
    if (len > 4 && (strcmp(it.full_path + len - 4, ".txt") == 0 ||
                    strcmp(it.full_path + len - 4, ".TXT") == 0)) {
      char *argv[] = {(char *)"TEXTVIEW.ELF", it.full_path, nullptr};
      serial_log("Explorer: Launching TextView for ");
      serial_log(it.full_path);
      sys_spawn("TEXTVIEW.ELF", argv);
      return;
    }

    // Generic file open (Simple serial log for now)
    int fd = sys_open(it.full_path, 0);
    if (fd < 0) {
      serial_log("Explorer: file open failed");
      return;
    }
    serial_log("Explorer: generic file read OK");
    sys_close(fd);
  }
}

// =======================================================
// DESKTOP FOLDER CREATION (REAL FS)
// =======================================================

extern "C" void explorer_create_folder(const char *name) {
  char path[MAX_PATH];
  build_child_path(path, cwd, name);

  serial_log("Explorer MKDIR: ");
  serial_log(path);

  int r = sys_mkdir(path, 0755);
  if (r < 0) {
    serial_log("MKDIR FAILED");
    return;
  }

  explorer_load_directory(cwd);
}

// =======================================================
// RIGHT CLICK DISPATCHER
// =======================================================

enum ContextActionType { CTX_OPEN, CTX_DELETE, CTX_PROPERTIES };

extern "C" void explorer_context_action(int index, int act) {
  if (index < 0 || index >= item_count)
    return;

  switch (act) {
  case CTX_OPEN:
    explorer_open_item(index);
    break;

  case CTX_DELETE:
    serial_log("DELETE not implemented yet");
    break;

  case CTX_PROPERTIES:
    serial_log("PATH: ");
    serial_log(items[index].full_path);
    break;
  }
}

// =======================================================
// DOUBLE CLICK DETECTION (GUI CALLS THIS)
// =======================================================

extern "C" void explorer_double_click(int index) { explorer_open_item(index); }

// =======================================================
// INITIALIZATION
// =======================================================

extern "C" void explorer_init() {
  serial_log("Explorer Init");
  explorer_load_directory("/home/user/Desktop");
}
