/* ============================================================
   FILE SYSTEM KERNEL ↔ GUI BRIDGE
   Single-file authoritative implementation

   GUI = client
   Kernel = owner
   FS = truth
   ============================================================ */

#include <stdbool.h>
#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ============================================================
   COMMON DEFINITIONS
   ============================================================ */

#define MAX_PATH 256
#define MAX_FILES 1024
#define MAX_OPEN_FILES 256
#define PAGE_SIZE 4096

/* ============================================================
   FILE SYSTEM REQUEST TYPES
   ============================================================ */

enum FsRequestType {
  FS_REQ_CREATE,
  FS_REQ_DELETE,
  FS_REQ_OPEN,
  FS_REQ_READDIR,
  FS_REQ_READ,
  FS_REQ_WRITE,
  FS_REQ_CLOSE
};

enum FsStatus {
  FS_OK = 0,
  FS_ERR = -1,
  FS_NOENT = -2,
  FS_PERM = -3,
};

/* ============================================================
   KERNEL FILE OBJECT (REAL OWNERSHIP)
   ============================================================ */

struct KernelFile {
  int id;
  char path[MAX_PATH];
  uint32_t size;
  uint8_t *cache; // memory-backed file cache
  bool dirty;
  bool in_use;
};

/* ============================================================
   VFS NODE (DIRECTORY TREE)
   ============================================================ */

struct VfsNode {
  char name[64];
  bool is_dir;
  int file_id; // index into kernel_files[]
};

/* ============================================================
   FS REQUEST / RESPONSE
   ============================================================ */

struct FsRequest {
  enum FsRequestType type;
  char path[MAX_PATH];
  void *buffer;
  uint32_t length;
};

struct FsResponse {
  int status;
  uint32_t out_count;
};

/* ============================================================
   GLOBAL KERNEL STATE
   ============================================================ */

static struct KernelFile kernel_files[MAX_FILES];
static int next_file_id = 1;

/* ============================================================
   MEMORY MANAGEMENT (FILE CACHE)
   ============================================================ */

void *kmem_alloc_page() {
  static uint8_t memory_pool[MAX_FILES][PAGE_SIZE];
  static int used = 0;
  if (used >= MAX_FILES)
    return NULL;
  return memory_pool[used++];
}

/* ============================================================
   VFS PRIMITIVES (SIMPLIFIED)
   ============================================================ */

int kf_create(const char *path) {
  for (int i = 0; i < MAX_FILES; i++) {
    if (!kernel_files[i].in_use) {
      kernel_files[i].in_use = true;
      kernel_files[i].id = next_file_id++;
      // strncpy substitute if not available
      int j = 0;
      while (path[j] && j < MAX_PATH - 1) {
        kernel_files[i].path[j] = path[j];
        j++;
      }
      kernel_files[i].path[j] = 0;

      kernel_files[i].size = 0;
      kernel_files[i].cache = (uint8_t *)kmem_alloc_page();
      kernel_files[i].dirty = true;
      return kernel_files[i].id;
    }
  }
  return FS_ERR;
}

struct KernelFile *kf_lookup(const char *path) {
  for (int i = 0; i < MAX_FILES; i++) {
    if (kernel_files[i].in_use) {
      // strcmp substitute if not available
      bool match = true;
      int j = 0;
      while (path[j] || kernel_files[i].path[j]) {
        if (path[j] != kernel_files[i].path[j]) {
          match = false;
          break;
        }
        j++;
      }
      if (match)
        return &kernel_files[i];
    }
  }
  return NULL;
}

/* ============================================================
   KERNEL FS DISPATCHER (THE HEART)
   ============================================================ */

int kernel_fs_dispatch(struct FsRequest *req, struct FsResponse *res) {

  if (!req || !res)
    return FS_ERR;

  switch (req->type) {

  case FS_REQ_CREATE: {
    int id = kf_create(req->path);
    res->status = (id > 0) ? FS_OK : FS_ERR;
    return res->status;
  }

  case FS_REQ_OPEN: {
    struct KernelFile *kf = kf_lookup(req->path);
    res->status = kf ? FS_OK : FS_NOENT;
    return res->status;
  }

  case FS_REQ_READ: {
    struct KernelFile *kf = kf_lookup(req->path);
    if (!kf)
      return FS_NOENT;
    uint32_t len = (req->length > kf->size) ? kf->size : req->length;
    // memcpy substitute
    for (uint32_t i = 0; i < len; i++)
      ((uint8_t *)req->buffer)[i] = kf->cache[i];
    res->out_count = len;
    return FS_OK;
  }

  case FS_REQ_WRITE: {
    struct KernelFile *kf = kf_lookup(req->path);
    if (!kf)
      return FS_NOENT;
    // memcpy substitute
    for (uint32_t i = 0; i < req->length; i++)
      kf->cache[i] = ((uint8_t *)req->buffer)[i];
    kf->size = req->length;
    kf->dirty = true;
    return FS_OK;
  }

  default:
    return FS_ERR;
  }
}

/* ============================================================
   SYSCALL ENTRY (GUI → KERNEL)
   ============================================================ */

#ifdef __cplusplus
extern "C"
#endif
    int sys_fs_call_impl(struct FsRequest *req, struct FsResponse *res) {
  // Validation, permission checks would go here
  return kernel_fs_dispatch(req, res);
}

/* ============================================================
   GUI-SIDE API (WHAT FILE EXPLORER USES)
   ============================================================ */
int sys_fs_call(struct FsRequest *req, struct FsResponse *res);

bool gui_create_file(const char *path) {
  struct FsRequest req;
  struct FsResponse res;
  // zero init
  uint8_t *p = (uint8_t *)&req;
  for (int i = 0; i < (int)sizeof(req); i++)
    p[i] = 0;
  p = (uint8_t *)&res;
  for (int i = 0; i < (int)sizeof(res); i++)
    p[i] = 0;

  req.type = FS_REQ_CREATE;
  int j = 0;
  while (path[j] && j < MAX_PATH - 1) {
    req.path[j] = path[j];
    j++;
  }
  req.path[j] = 0;

  return sys_fs_call(&req, &res) == FS_OK;
}

bool gui_read_file(const char *path, void *buf, uint32_t len) {
  struct FsRequest req;
  struct FsResponse res;
  uint8_t *p = (uint8_t *)&req;
  for (int i = 0; i < (int)sizeof(req); i++)
    p[i] = 0;
  p = (uint8_t *)&res;
  for (int i = 0; i < (int)sizeof(res); i++)
    p[i] = 0;

  req.type = FS_REQ_READ;
  int j = 0;
  while (path[j] && j < MAX_PATH - 1) {
    req.path[j] = path[j];
    j++;
  }
  req.path[j] = 0;

  req.buffer = buf;
  req.length = len;
  return sys_fs_call(&req, &res) == FS_OK;
}

bool gui_write_file(const char *path, void *buf, uint32_t len) {
  struct FsRequest req;
  struct FsResponse res;
  uint8_t *p = (uint8_t *)&req;
  for (int i = 0; i < (int)sizeof(req); i++)
    p[i] = 0;
  p = (uint8_t *)&res;
  for (int i = 0; i < (int)sizeof(res); i++)
    p[i] = 0;

  req.type = FS_REQ_WRITE;
  int j = 0;
  while (path[j] && j < MAX_PATH - 1) {
    req.path[j] = path[j];
    j++;
  }
  req.path[j] = 0;

  req.buffer = buf;
  req.length = len;
  return sys_fs_call(&req, &res) == FS_OK;
}
