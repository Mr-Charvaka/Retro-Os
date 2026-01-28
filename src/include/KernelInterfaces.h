/*
========================================================
 STAGE 2 — KERNEL INTERFACE HARDENING
========================================================

Purpose:
- Freeze syscall semantics
- Prevent abstraction drift
- Make kernel <-> userspace contracts explicit
- Enable safe future evolution (POSIX, IPC, GUI, etc.)

Inspired by:
- SerenityOS syscall layer
- Linux uapi philosophy
- BeOS kernel contracts

This file defines WHAT the kernel promises.
NOT how it is implemented.
*/

#ifndef KERNEL_INTERFACES_H
#define KERNEL_INTERFACES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/* ======================================================
   COMMON TYPES & VERSIONING
   ====================================================== */

#define KERNEL_API_VERSION 1

typedef int32_t KResult;

/*
ERROR MODEL:
- 0  = success
- <0 = error
*/

enum KError : KResult {
  K_OK = 0,
  K_EINVAL = -1,
  K_ENOENT = -2,
  K_EPERM = -3,
  K_EIO = -4,
  K_ENOMEM = -5,
  K_EBUSY = -6,
  K_ENOTSUP = -7,
  K_EBADF = -8,
};

/*
All kernel-visible structs MUST:
- be POD
- be versioned
- never change layout silently
*/

struct KStructHeader {
  uint32_t version;
  uint32_t size;
};

/* ======================================================
   HANDLE MODEL (CRITICAL)
   ====================================================== */

/*
Kernel objects are NEVER exposed directly.
Everything is accessed through handles.
*/

typedef int32_t KHandle;

#define KHANDLE_INVALID -1

/* ======================================================
   PROCESS & THREAD INTERFACE
   ====================================================== */

typedef int32_t PID;
typedef int32_t TID;

struct KProcessInfo {
  KStructHeader hdr;
  PID pid;
  uint32_t thread_count;
};

#ifdef __cplusplus
extern "C" {
#endif

/*
Spawn a new process.

SEMANTICS (FROZEN):
- path must be absolute
- process starts suspended
- returns PID or error
*/
KResult sys_spawn_process(const char *path, PID *out_pid);

/*
Terminate a process.

SEMANTICS:
- safe at any time
- must release all resources
*/
KResult sys_kill_process(PID pid);

/*
Query process info.
*/
KResult sys_get_process_info(PID pid, KProcessInfo *out_info);

/* ======================================================
   FILESYSTEM INTERFACE (HARDENED)
   ====================================================== */

struct KDirEntry {
  char name[256];
  uint8_t is_directory;
};

struct KReaddirResult {
  KStructHeader hdr;
  uint32_t entry_count;
  KDirEntry entries[];
};

KResult sys_fs_exists(const char *path, uint8_t *out_exists);
KResult sys_fs_mkdir(const char *path);
KResult sys_fs_remove(const char *path);

/*
Caller provides buffer.
Kernel fills as much as fits.
*/
KResult sys_fs_readdir(const char *path, KReaddirResult *buffer,
                       size_t buffer_size);

/* ======================================================
   MEMORY & ADDRESS SPACE
   ====================================================== */

struct KMemoryInfo {
  KStructHeader hdr;
  uint64_t total_bytes;
  uint64_t used_bytes;
};

KResult sys_query_memory(KMemoryInfo *out_info);

/* ======================================================
   GRAPHICS & SURFACE INTERFACE
   ====================================================== */

struct KSurfaceInfo {
  KStructHeader hdr;
  uint32_t width;
  uint32_t height;
};

KResult sys_surface_create(uint32_t width, uint32_t height,
                           KHandle *out_surface);

KResult sys_surface_destroy(KHandle surface);

KResult sys_surface_map(KHandle surface, void **out_pixels);

KResult sys_surface_present(KHandle surface);

/* ======================================================
   INPUT INTERFACE
   ====================================================== */

typedef uint32_t KInputType;
#define KINPUT_MOUSE 0
#define KINPUT_KEYBOARD 1

struct KInputEvent {
  KStructHeader hdr;
  KInputType type;
  uint32_t code;
  int32_t x;
  int32_t y;
};

/*
Non-blocking input poll.
*/
KResult sys_poll_input(KInputEvent *out_event);

/* ======================================================
   IPC INTERFACE (HARDENED)
   ====================================================== */

#define IPC_MAX_PAYLOAD 256

struct KIPCMessage {
  KStructHeader hdr;
  PID sender;
  uint32_t type;
  uint32_t payload_size;
  uint8_t payload[IPC_MAX_PAYLOAD];
};

KResult sys_ipc_send(PID target, const KIPCMessage *msg);
KResult sys_ipc_receive(KIPCMessage *out_msg);

/* ======================================================
   BOOT-TIME INTERFACE SELF-TEST
   ====================================================== */

void kernel_interface_self_test();

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static inline void validate_struct(const KStructHeader *hdr,
                                   uint32_t expected_version,
                                   size_t expected_size) {
  if (!hdr)
    while (true) {
    }

  if (hdr->version != expected_version)
    while (true) {
    }

  if (hdr->size < expected_size)
    while (true) {
    }
}
#endif

#endif
