/* ============================================================
   EXPLORER KERNEL CONTRACT
   Single-file authoritative implementation

   GUI = client
   Kernel = owner
   FS = truth
   ============================================================ */

#include "../include/string.h"
#include <stdbool.h>
#include <stdint.h>

/* 🧩 1️⃣ Shared Contracts (GUI ↔ Kernel) */
#define MAX_PATH 256
#define MAX_NAME 64
#define MAX_FILES 1024

/* 🧩 2️⃣ Kernel Data Structures (REAL) */
struct FileNode {
  char name[MAX_NAME];
  uint32_t size;
  uint32_t data_block;
  bool is_directory;
};

struct Directory {
  char path[MAX_PATH];
  FileNode files[MAX_FILES];
  uint32_t file_count;
};

static Directory root_dir;
static FileNode clipboard;
static bool clipboard_valid = false;

/* 🧩 3️⃣ Kernel Syscall Definitions */
enum SyscallID {
  SYS_CREATE_FILE,
  SYS_DELETE_FILE,
  SYS_RENAME_FILE,
  SYS_COPY_FILE,
  SYS_PASTE_FILE,
  SYS_GET_FILES // New: Get listing
};

struct SyscallPacket {
  SyscallID id;
  char path[MAX_PATH];
  char name[MAX_NAME];
  char new_name[MAX_NAME];
  void *buffer; // For directory listing
  uint32_t buffer_size;
  bool is_dir; // Flag for creation
};

/* 🧩 4️⃣ Kernel Filesystem Logic (REAL) */

// Persistence Helpers (Kernel Internal API)
#include "../include/vfs.h"

static void k_save_state() {
  vfs_node_t *node = vfs_resolve_path("/explorer.db");
  if (!node) {
    vfs_create("/explorer.db", VFS_FILE);
    node = vfs_resolve_path("/explorer.db");
  }
  if (node) {
    vfs_write(node, 0, (uint8_t *)&root_dir, sizeof(root_dir));
  }
}

static void k_load_state() {
  static bool loaded = false;
  if (loaded)
    return;
  vfs_node_t *node = vfs_resolve_path("/explorer.db");
  if (node) {
    vfs_read(node, 0, (uint8_t *)&root_dir, sizeof(root_dir));
  }
  loaded = true;
}

// Create File/Folder
bool k_create_file(const char *name, bool is_dir) {
  k_load_state();
  if (root_dir.file_count >= MAX_FILES)
    return false;

  FileNode *f = &root_dir.files[root_dir.file_count++];
  int i = 0;
  while (name[i] && i < MAX_NAME - 1) {
    f->name[i] = name[i];
    i++;
  }
  f->name[i] = 0;
  f->size = 0;
  f->data_block = 0;
  f->is_directory = is_dir;
  k_save_state();
  return true;
}

// Delete File
bool k_delete_file(const char *name) {
  k_load_state();
  for (uint32_t i = 0; i < root_dir.file_count; i++) {
    if (!strcmp(root_dir.files[i].name, name)) {
      root_dir.files[i] = root_dir.files[--root_dir.file_count];
      k_save_state();
      return true;
    }
  }
  return false;
}

// Rename File
bool k_rename_file(const char *old, const char *newn) {
  k_load_state();
  for (uint32_t i = 0; i < root_dir.file_count; i++) {
    if (!strcmp(root_dir.files[i].name, old)) {
      int j = 0;
      while (newn[j] && j < MAX_NAME - 1) {
        root_dir.files[i].name[j] = newn[j];
        j++;
      }
      root_dir.files[i].name[j] = 0;
      k_save_state();
      return true;
    }
  }
  return false;
}

// Copy File (Kernel Clipboard)
bool k_copy_file(const char *name) {
  k_load_state();
  for (uint32_t i = 0; i < root_dir.file_count; i++) {
    if (!strcmp(root_dir.files[i].name, name)) {
      clipboard = root_dir.files[i];
      clipboard_valid = true;
      return true;
    }
  }
  return false;
}

// Paste File
bool k_paste_file() {
  k_load_state();
  if (!clipboard_valid)
    return false;
  if (root_dir.file_count >= MAX_FILES)
    return false;

  FileNode *f = &root_dir.files[root_dir.file_count++];
  *f = clipboard;
  k_save_state();
  return true;
}

// Get Listing
int k_get_files(void *buf, uint32_t size) {
  k_load_state();
  uint32_t total = root_dir.file_count * sizeof(FileNode);
  if (total > size)
    total = size;

  uint8_t *dst = (uint8_t *)buf;
  uint8_t *src = (uint8_t *)root_dir.files;
  for (uint32_t i = 0; i < total; i++)
    dst[i] = src[i];

  return root_dir.file_count;
}

/* 🧩 5️⃣ Kernel Syscall Dispatcher (CRITICAL) */
extern "C" int kernel_syscall(SyscallPacket *p) {
  if (!p)
    return -1;
  switch (p->id) {
  case SYS_CREATE_FILE:
    return k_create_file(p->name, p->is_dir);

  case SYS_DELETE_FILE:
    return k_delete_file(p->name);

  case SYS_RENAME_FILE:
    return k_rename_file(p->name, p->new_name);

  case SYS_COPY_FILE:
    return k_copy_file(p->name);

  case SYS_PASTE_FILE:
    return k_paste_file();

  case SYS_GET_FILES:
    return k_get_files(p->buffer, p->buffer_size);
  }
  return -1;
}

/* 🧩 6️⃣ GUI SIDE (Explorer Right-Click) */
// GUI sends intent ONLY
void explorer_right_click_create(const char *name) {
  SyscallPacket p = {SYS_CREATE_FILE};
  strcpy(p.name, name);
  kernel_syscall(&p);
}

void explorer_right_click_delete(const char *name) {
  SyscallPacket p = {SYS_DELETE_FILE};
  strcpy(p.name, name);
  kernel_syscall(&p);
}

/* 🧩 7️⃣ Context Menu Mapping (Per-App) */
enum ExplorerAction {
  EXPLORER_ACTION_NEW,
  EXPLORER_ACTION_DELETE,
  EXPLORER_ACTION_RENAME,
  EXPLORER_ACTION_COPY,
  EXPLORER_ACTION_PASTE
};

extern "C" void explorer_context_execute(ExplorerAction a, const char *target) {
  switch (a) {
  case EXPLORER_ACTION_NEW:
    explorer_right_click_create("NewFile.txt");
    break;
  case EXPLORER_ACTION_DELETE:
    explorer_right_click_delete(target);
    break;
  case EXPLORER_ACTION_COPY: {
    SyscallPacket p = {SYS_COPY_FILE};
    strcpy(p.name, target);
    kernel_syscall(&p);
  } break;
  case EXPLORER_ACTION_PASTE: {
    SyscallPacket p = {SYS_PASTE_FILE};
    kernel_syscall(&p);
  } break;
  case EXPLORER_ACTION_RENAME: {
    SyscallPacket p = {SYS_RENAME_FILE};
    strcpy(p.name, target);
    strcpy(p.new_name, "Renamed.txt"); // Mock target name
    kernel_syscall(&p);
  } break;
  }
}
