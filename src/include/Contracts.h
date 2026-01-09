/*
========================================================
 SYSTEM CONTRACTS — STAGE 1
========================================================

This file defines the NON-NEGOTIABLE architectural laws
of the operating system.

Inspired by:
- SerenityOS
- BeOS
- early UNIX philosophy

This file is intentionally strict.
Violations are bugs, not features.
*/

#ifndef SYSTEM_CONTRACTS_H
#define SYSTEM_CONTRACTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================
   KERNEL PANIC & ASSERTION
   ====================================================== */

[[noreturn]] void kernel_panic(const char *reason);

#define CONTRACT_ASSERT(expr, msg)                                             \
  do {                                                                         \
    if (!(expr))                                                               \
      kernel_panic(msg);                                                       \
  } while (0)

/* ======================================================
   GLOBAL SYSTEM INVARIANTS
   ====================================================== */

/*
SYSTEM-WIDE LAWS:

1. Every piece of persistent state has EXACTLY ONE owner.
2. Filesystem is the ONLY persistent storage of user data.
3. Kernel is the final authority.
4. Killing any process must never corrupt system state.
5. No subsystem may mirror another subsystem’s truth.
*/

/* ======================================================
   FILESYSTEM CONTRACT
   ====================================================== */

struct FS_DirEntry {
  char name[128];
  bool is_directory;
};

struct FS_ReadResult {
  FS_DirEntry *entries;
  uint32_t count;
};

/*
FILESYSTEM RULES:
- Disk is the source of truth
- No UI-visible virtualization
- No filesystem caching outside VFS
*/

FS_ReadResult fs_readdir(const char *path);
bool fs_create_dir(const char *path);
bool fs_remove(const char *path);
bool fs_exists(const char *path);

static inline void fs_contract_validate_path(const char *path) {
  CONTRACT_ASSERT(path != nullptr, "FS: null path");
  CONTRACT_ASSERT(path[0] == '/', "FS: path must be absolute");
}

/* ======================================================
   PROCESS CONTRACT
   ====================================================== */

typedef int32_t PID;

/*
PROCESS RULES:
- Processes are isolated
- Processes are disposable
- No process owns global system state
*/

PID spawn_process(const char *path);
bool kill_process(PID pid);

static inline void process_contract_validate(PID pid) {
  CONTRACT_ASSERT(pid >= 0, "Invalid PID");
}

/* ======================================================
   IPC CONTRACT
   ====================================================== */

struct IPC_Message {
  uint32_t type;
  uint32_t size;
  const void *payload; // immutable
};

bool ipc_send(PID target, const IPC_Message &msg);
bool ipc_receive(IPC_Message &msg);

/* ======================================================
   SYSTEM SELF-TEST (BOOT-TIME DEFENSE)
   ====================================================== */

void system_self_test();

#ifdef __cplusplus
}
#endif

#endif
