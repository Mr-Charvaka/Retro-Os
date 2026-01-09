#ifndef FS_PHASE_H
#define FS_PHASE_H

#include <stddef.h>
#include <stdint.h>

#define MAX_PATH 256
#define MAX_NAME 64
#define BLOCK_SIZE 4096

#define INODE_FILE 1
#define INODE_DIR 2

#define PERM_READ 0x4
#define PERM_WRITE 0x2
#define PERM_EXEC 0x1

#ifdef __cplusplus
extern "C" {
#endif

// Directory Entry
struct phase_dir_entry {
  char name[MAX_NAME];
  uint32_t inode_id;
};

// Inode
struct phase_inode {
  uint32_t id;
  uint32_t type;
  uint32_t size;
  uint32_t blocks[12];
  uint16_t owner;
  uint16_t group;
  uint16_t perms;
};

// Shared Globals (Implemented in fs_phase_a.cpp)
extern phase_inode phase_inode_table[256];
extern uint8_t phase_data_blocks[64][BLOCK_SIZE];
extern uint32_t phase_inode_count;
extern uint32_t phase_block_count;

// Phase A Functions
phase_inode *phase_vfs_resolve(const char *path);
uint32_t phase_alloc_block();
phase_inode *phase_alloc_inode(uint32_t type);
bool phase_vfs_check_perm(phase_inode *node, uint16_t uid, uint8_t req);
bool phase_vfs_rename(const char *oldpath, const char *newpath);
bool vfs_mkdir_phase_a(const char *path);
int vfs_create_file_phase_a(const char *path);
int vfs_write_phase_a(phase_inode *f, const char *data, uint32_t len);

// Persistence Interface
void phase_vfs_sync();
void phase_vfs_load();
uint32_t phase_vfs_get_total_size();

// Phase B Functions
bool cmd_cd(const char *path);
void cmd_pwd();
void cmd_ls(const char *path);
void cmd_stat(const char *path);
int sys_ls_direct(const char *path, phase_dir_entry *out, int max);
int sys_stat_direct(const char *path, phase_inode *out);
int sys_read_direct(const char *path, char *buf, int max);

// Phase C Types
enum ContextTarget { TARGET_DESKTOP, TARGET_FILE, TARGET_FOLDER, TARGET_EMPTY };
enum ContextAction {
  ACT_REFRESH,
  ACT_OPEN,
  ACT_NEW_FOLDER,
  ACT_NEW_FILE,
  ACT_DELETE,
  ACT_RENAME,
  ACT_OPEN_TERMINAL,
  ACT_DISPLAY_SETTINGS,
  ACT_PERSONALIZE,
  ACT_SORT,
  ACT_UNDO,
  ACT_REDO,
  ACT_RESTORE,
  ACT_PURGE,
  ACT_SELECT,
  ACT_SELECT_ALL,
  ACT_PROPERTIES
};

struct ContextIntent {
  ContextTarget target;
  ContextAction action;
  char path[MAX_PATH];
  char new_path[MAX_PATH];
  char extra[MAX_PATH];
};

enum FileAction { ACTION_OPEN, ACTION_EXEC, ACTION_VIEW, ACTION_NONE };

// Phase C Functions
int sys_context_execute(ContextIntent *i);
void fs_phase_a_bootstrap();
void fs_properties(const char *path);

#ifdef __cplusplus
}
#endif

#endif
