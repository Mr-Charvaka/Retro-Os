#include "../include/kernel_fs_phase3.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h" // For timestamp? Or generic.
#include "../include/string.h"
#include "heap.h"
#include "memory.h"

extern "C" {

/* ==========================================================
   GLOBALS
   ========================================================== */

#define MAX_USERS 8
#define MAX_JOURNAL 256
#define MAX_HISTORY 64

static struct user users[MAX_USERS];
static int user_count = 0;
static int active_user_idx = 0;

static struct journal_entry journal[MAX_JOURNAL];
static int journal_head = 0;

struct history_entry {
  int type;
  uint32_t inode;
  char name[MAX_FILENAME];
  uint32_t parent;
};

static struct history_entry undo_stack[MAX_HISTORY];
static int undo_ptr = 0;
static struct history_entry redo_stack[MAX_HISTORY];
static int redo_ptr = 0;

/* ==========================================================
   USER MANAGEMENT
   ========================================================== */

void user_add(const char *username) {
  if (user_count >= MAX_USERS)
    return;
  users[user_count].uid = user_count;
  strncpy(users[user_count].username, username, 31);
  user_count++;
  serial_log("OFFSET_FS: Added user");
}

void user_switch(const char *username) {
  for (int i = 0; i < user_count; i++) {
    if (strcmp(users[i].username, username) == 0) {
      active_user_idx = i;
      return;
    }
  }
}

uint32_t current_uid() { return users[active_user_idx].uid; }

bool has_permission(uint32_t inode_id, int required_perm) {
  struct inode *node = &inode_table[inode_id];

  // Root (0) generally has access
  if (current_uid() == 0)
    return true;

  // Owner check
  if (node->uid == current_uid()) {
    if ((node->permissions >> 6) & required_perm)
      return true;
  }

  // Others (simplified)
  if ((node->permissions) & required_perm)
    return true;

  return false;
}

/* ==========================================================
   JOURNALING
   ========================================================== */

void journal_log(int type, uint32_t inode, const char *name) {
  int idx = journal_head % MAX_JOURNAL;
  journal[idx].type = type;
  journal[idx].inode = inode;
  if (name)
    strncpy(journal[idx].name, name, MAX_FILENAME - 1);
  journal[idx].timestamp = 0; // needs timer
  journal_head++;

  serial_log("FS_JOURNAL: Logged operation");
}

void journal_replay() {
  serial_log("FS_JOURNAL: Replaying... (Nothing to do yet)");
}

/* ==========================================================
   UNDO / REDO
   ========================================================== */

void history_push(int type, uint32_t inode, const char *name) {
  if (undo_ptr >= MAX_HISTORY)
    return; // Full
  undo_stack[undo_ptr].type = type;
  undo_stack[undo_ptr].inode = inode;
  if (name)
    strncpy(undo_stack[undo_ptr].name, name, MAX_FILENAME - 1);
  undo_ptr++;
  redo_ptr = 0; // Clear redo on new action
}

void undo_last() {
  if (undo_ptr <= 0)
    return;
  undo_ptr--;
  struct history_entry *act = &undo_stack[undo_ptr];

  serial_log("FS_UNDO: Undoing last action");

  // Logic to revert:
  // If Create -> Delete
  // If Write -> (Need old content, complex)
  // If Delete -> Restore (Need Recycle Bin)

  // For now, simple logging stub
}

void redo_last() {
  // Similar stub
}

/* ==========================================================
   SECURE OPERATIONS
   ========================================================== */

int fs_create_secure(uint32_t parent, const char *name, int type,
                     uint32_t perms) {
  // 1. Permission Check
  if (!has_permission(parent, PERM_WRITE)) {
    serial_log("FS_SECURE: Access Denied (Create)");
    return -1;
  }

  // 2. Journal Start
  journal_log(1, 0, name); // 1 = Create start

  // 3. Execution
  int inode = fs_create_internal(parent, name, (enum inode_type)type);

  if (inode >= 0) {
    // Set metadata
    inode_table[inode].uid = current_uid();
    inode_table[inode].gid = current_uid(); // simplified
    inode_table[inode].permissions = perms;

    // 4. History
    history_push(1, inode, name);
  }

  return inode;
}

int fs_write_secure(uint32_t inode, const void *buf, uint32_t size) {
  if (!has_permission(inode, PERM_WRITE))
    return -1;

  journal_log(2, inode, "write");
  int res = fs_write_internal(inode, buf, size);
  if (res > 0)
    history_push(2, inode, "write");
  return res;
}

int fs_delete_secure(uint32_t parent, const char *name) {
  if (!has_permission(parent, PERM_WRITE))
    return -1;

  // TODO: fs_delete_internal implementation or unlink
  // For now, lookup and mark unused?
  // fs_delete_internal not exposed yet.
  return 0;
}

/* ==========================================================
   INIT
   ========================================================== */

void fs_phase3_init() {
  // Add Root User
  user_add("root");
  user_add("guest");

  user_switch("root");

  serial_log("FS_PHASE3: Security Layer Active");
}

// Wrappers for Kernel.cpp legacy calls
void fs_undo() { undo_last(); }
void fs_redo() { redo_last(); }
void fs_restore(int inode) {
  (void)inode;
  serial_log("FS: Restore not impl");
}
void fs_purge(int inode) {
  (void)inode;
  serial_log("FS: Purge not impl");
}
int fs_delete(int inode) {
  (void)inode;
  serial_log("FS: Delete not impl");
  return 0;
}

} // extern "C"
