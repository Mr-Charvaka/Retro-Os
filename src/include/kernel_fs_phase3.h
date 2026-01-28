#ifndef KERNEL_FS_PHASE3_H
#define KERNEL_FS_PHASE3_H

#include "kernel_fs_internal.h"
#include "types.h"

// Phase 3 Permissions
#define PERM_READ 0x4
#define PERM_WRITE 0x2
#define PERM_EXEC 0x1

// Phase 3 Structures
struct user {
  uint32_t uid;
  char username[32];
};

struct journal_entry {
  uint32_t type; // 1=Create, 2=Write, 3=Delete
  uint32_t inode;
  uint32_t parent;
  char name[MAX_FILENAME];
  uint32_t timestamp;
};

#ifdef __cplusplus
extern "C" {
#endif

// Functions exposed to Kernel/GUI
void fs_phase3_init();
int fs_create_secure(uint32_t parent, const char *name, int type,
                     uint32_t perms);
int fs_write_secure(uint32_t inode, const void *buf, uint32_t size);
int fs_delete_secure(uint32_t parent, const char *name);

// User Management
void user_add(const char *username);
void user_switch(const char *username);
uint32_t current_uid();

// Journaling
void journal_log(int type, uint32_t inode, const char *name);
void journal_replay();

// Undo/Redo
void history_push(int type, uint32_t inode, const char *name);
void undo_last();
void redo_last();

#ifdef __cplusplus
}
#endif

#endif
