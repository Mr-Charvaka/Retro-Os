/*
 * fs_phase_c.cpp
 *
 * PHASE C: Functional Authority Layer
 */

#include <KernelInterfaces.h>
#include <fs_phase.h>
#include <string.h>


extern "C" void serial_log(const char *msg);
extern "C" void serial_log_int(int val);
extern "C" void compositor_invalidate();

// Basename helper
static void phase_basename(const char *path, char *name) {
  if (!path || !name)
    return;
  int len = strlen(path);
  if (len == 0) {
    strcpy(name, "");
    return;
  }
  int i = len - 1;
  while (i >= 0 && path[i] != '/')
    i--;
  strcpy(name, path + i + 1);
}

// 🔹 SECTION 1: FILE ASSOCIATION ENGINE
static FileAction file_associate(const char *name) {
  if (!name)
    return ACTION_NONE;
  int len = strlen(name);
  if (len > 4 && strcmp(name + len - 4, ".txt") == 0)
    return ACTION_VIEW;
  if (len > 4 && strcmp(name + len - 4, ".bin") == 0)
    return ACTION_EXEC;
  return ACTION_OPEN;
}

// 🔹 SECTION 2: OPEN DISPATCHER
static void fs_open_dispatch(const char *path) {
  phase_inode meta;
  if (sys_stat_direct(path, &meta) != 0)
    return;

  if (meta.type == INODE_DIR) {
    cmd_cd(path);
    return;
  }

  FileAction act = file_associate(path);

  if (act == ACTION_VIEW) {
    char buf[2048];
    int n = sys_read_direct(path, buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = 0;
      serial_log("FS_VIEW: ");
      serial_log(buf);
    }
  } else if (act == ACTION_EXEC) {
    PID out_pid;
    sys_spawn_process(path, &out_pid);
  }
}

// 🔹 SECTION 3: RECYCLE BIN (SOFT DELETE)
#define RECYCLE_PATH "/system/.recycle"

static bool fs_soft_delete(const char *path) {
  char name[64];
  phase_basename(path, name);

  char target[128];
  strcpy(target, RECYCLE_PATH);
  strcat(target, "/");
  strcat(target, name);

  return phase_vfs_rename(path, target);
}

// 🔹 SECTION 4: UNDO / REDO KERNEL HISTORY
enum HistoryOp { OP_DELETE, OP_RENAME };

struct HistoryEntry {
  HistoryOp op;
  char src[MAX_PATH];
  char dst[MAX_PATH];
};

static HistoryEntry history_buffer[64];
static int hist_top = 0;

static void history_push(HistoryOp op, const char *a, const char *b) {
  if (hist_top >= 64)
    return;
  history_buffer[hist_top].op = op;
  if (a)
    strcpy(history_buffer[hist_top].src, a);
  if (b)
    strcpy(history_buffer[hist_top].dst, b);
  hist_top++;
}

static void history_undo() {
  if (hist_top == 0)
    return;
  HistoryEntry &h = history_buffer[--hist_top];

  if (h.op == OP_DELETE) {
    phase_vfs_rename(h.dst, h.src);
  }
}

// 🔹 SECTION 5: RIGHT-CLICK INTENT HANDLER (GUI → KERNEL)
extern "C" int sys_context_execute(ContextIntent *i) {
  if (!i)
    return -1;

  switch (i->action) {
  case ACT_REFRESH:
    compositor_invalidate();
    return 0;

  case ACT_OPEN:
    fs_open_dispatch(i->path);
    return 0;

  case ACT_NEW_FOLDER: {
    char full[MAX_PATH];
    strcpy(full, i->path);
    int len = strlen(full);
    if (len > 0 && full[len - 1] != '/')
      strcat(full, "/");
    strcat(full, "New Folder");
    return vfs_mkdir_phase_a(full) ? 0 : -1;
  }

  case ACT_NEW_FILE: {
    char full[MAX_PATH];
    strcpy(full, i->path);
    int len = strlen(full);
    if (len > 0 && full[len - 1] != '/')
      strcat(full, "/");
    strcat(full, "NewFile.txt");
    return vfs_create_file_phase_a(full) >= 0 ? 0 : -1;
  }

  case ACT_DELETE: {
    char name[64];
    phase_basename(i->path, name);
    char target[128];
    strcpy(target, RECYCLE_PATH);
    strcat(target, "/");
    strcat(target, name);

    history_push(OP_DELETE, i->path, target);
    return fs_soft_delete(i->path) ? 0 : -1;
  }

  case ACT_RENAME:
    // GUI provides new name via new_path or extra
    return phase_vfs_rename(i->path, i->new_path) ? 0 : -1;

  case ACT_UNDO:
    history_undo();
    return 0;

  case ACT_PROPERTIES:
    fs_properties(i->path);
    return 0;

  default:
    break;
  }
  return -1;
}

// 🔹 SECTION 6: FILE PROPERTIES (STAT → GUI)
extern "C" void fs_properties(const char *path) {
  phase_inode f;
  if (sys_stat_direct(path, &f) != 0)
    return;

  serial_log("Name: ");
  serial_log(path);
  serial_log("Size: ");
  serial_log_int(f.size);
  serial_log("Owner: ");
  serial_log_int((int)f.owner);
  serial_log("Perms: ");
  serial_log_int((int)f.perms);
}
