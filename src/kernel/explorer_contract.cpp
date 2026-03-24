// explorer_contract.cpp
// HARD CONTRACT between File Explorer GUI and Kernel VFS
// 32-bit, absolute-path enforced, crash-safe logic

#include "../drivers/serial.h"
#include "../include/string.h"
#include "../include/types.h"
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
int vfs_get_mount_count();
const char *vfs_get_mount_path(int i);
}

// =======================================================
// EXPLORER DATA MODEL (NO FAKE DATA ALLOWED)
// =======================================================

struct ExplorerItem {
  char name[64];
  char full_path[MAX_PATH]; // ABSOLUTE PATH
  uint32_t type;
};

struct ExplorerState {
  ExplorerItem items[MAX_ITEMS];
  int item_count;
  char cwd[MAX_PATH];
  int selected;
  int hovered;
  int scroll_y;
  bool active;
  
  // Flagship Disk Stats
  struct DriveInfo {
    char label[32];
    char path[32];
    uint32_t total;
    uint32_t free;
  } drives[8];
  int drive_count;
  bool show_disk_usage;
};

extern "C" int vfs_get_disk_usage(const char *path, uint32_t *total, uint32_t *free);

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

extern "C" void explorer_load_directory_ex(ExplorerState *state, const char *path) {
  if (!state) return;
  state->item_count = 0;
  strcpy(state->cwd, path);
  state->show_disk_usage = false;

  if (strcmp(path, "computer:") == 0) {
      state->show_disk_usage = true;
      state->drive_count = 0;
      state->item_count = 0; // Clear items for This PC view
      
      int n = vfs_get_mount_count();
      for (int i=0; i<n && state->drive_count < 8; i++) {
          const char* mp = vfs_get_mount_path(i);
          if (mp) {
              auto &d = state->drives[state->drive_count++];
              strcpy(d.path, mp);
              if (strcmp(mp, "/") == 0) strcpy(d.label, "System (Root)");
              else if (strcmp(mp, "/C") == 0) strcpy(d.label, "Local Disk (C:)");
              else {
                  strcpy(d.label, "Drive ");
                  strcat(d.label, mp+1);
              }
              vfs_get_disk_usage(mp, &d.total, &d.free);

              // Also add to items so they are clickable icons
              auto &item = state->items[state->item_count++];
              strcpy(item.name, d.label);
              item.type = 2; // TYPE_DIR
              strcpy(item.full_path, d.path);
              // We'll use a special check in explorer_open_item_ex to handle this
          }
      }
      return; 
  }

  if (strcmp(path, "/C") == 0 || strcmp(path, "/") == 0) {
      // Don't show disk usage bars here anymore, only in computer:
      state->show_disk_usage = false;
  }

  int fd = sys_open(path, 0);
  if (fd < 0) return;

  for (int i = 0; i < MAX_ITEMS; i++) {
    struct dirent ent;

    if (sys_readdir(fd, i, &ent) != 0)
      break;

    if (!ent.d_name[0] || strcmp(ent.d_name, ".") == 0 ||
        strcmp(ent.d_name, "..") == 0)
      continue;

    ExplorerItem &it = state->items[state->item_count++];
    strcpy(it.name, ent.d_name);
    // DT_DIR is 4 in dirent.h. TYPE_DIR in contract is 2.
    it.type = (ent.d_type == 4) ? 2 : 1; 
    build_child_path(it.full_path, path, ent.d_name);
  }

  sys_close(fd);
}

extern "C" void explorer_load_directory(const char *path) {
    // Deprecated global version, do nothing or use a fallback
}

extern "C" void explorer_init() {
    // Deprecated global version
}

extern "C" void explorer_open_item(int index) {
    // Deprecated global version
}
