#include "../drivers/serial.h"
#include "Contracts.h"
#include "KernelInterfaces.h"
#include "string.h"

/*
=========================================================
 STAGE 5 — FILESYSTEM TRUTH
=========================================================
*/

extern "C" void *kmalloc(uint32_t size);

namespace FileSystemTruth {

enum class NodeType : uint8_t { File, Directory };

struct FSNode {
  const char *name;
  NodeType type;
  FSNode *parent;
  FSNode *children[32]; // max 32 children per directory
  uint32_t child_count;
};

/* ======================================================
   ROOT DIRECTORY
   ====================================================== */

static FSNode g_root = {"/", NodeType::Directory, nullptr, {}, 0};

/* ======================================================
   HELPER: CREATE NODE
   ====================================================== */

FSNode *create_node(const char *name, NodeType type, FSNode *parent) {
  CONTRACT_ASSERT(parent != nullptr, "FS Truth: Parent must exist");
  CONTRACT_ASSERT(parent->child_count < 32, "FS Truth: Directory full");

  // We manually allocate since we want to avoid 'new' conflicts
  FSNode *n = (FSNode *)kmalloc(sizeof(FSNode));
  memset(n, 0, sizeof(FSNode));
  n->name = name;
  n->type = type;
  n->parent = parent;
  n->child_count = 0;
  parent->children[parent->child_count++] = n;
  return n;
}

/* ======================================================
   PATH RESOLUTION (ABSOLUTE ONLY)
   ====================================================== */

FSNode *fs_resolve(const char *path) {
  if (strcmp(path, "/") == 0)
    return &g_root;

  FSNode *current = &g_root;
  const char *p = path;

  if (*p == '/')
    p++;

  char token[64];

  while (*p) {
    int i = 0;
    while (*p && *p != '/')
      token[i++] = *p++;
    token[i] = 0;
    if (*p == '/')
      p++;

    bool found = false;
    for (uint32_t c = 0; c < current->child_count; c++) {
      if (strcmp(current->children[c]->name, token) == 0) {
        current = current->children[c];
        found = true;
        break;
      }
    }
    if (!found)
      return nullptr;
  }

  return current;
}

/* ======================================================
   FILESYSTEM OPERATIONS
   ====================================================== */

bool fs_mkdir(const char *path, const char *name) {
  FSNode *parent = fs_resolve(path);
  if (!parent || parent->type != NodeType::Directory)
    return false;

  create_node(name, NodeType::Directory, parent);
  return true;
}

bool fs_create_file(const char *path, const char *name) {
  FSNode *parent = fs_resolve(path);
  if (!parent || parent->type != NodeType::Directory)
    return false;

  create_node(name, NodeType::File, parent);
  return true;
}

bool fs_remove(const char *path) {
  FSNode *node = fs_resolve(path);
  if (!node || node == &g_root)
    return false;
  if (node->type == NodeType::Directory && node->child_count > 0)
    return false;

  FSNode *parent = node->parent;
  CONTRACT_ASSERT(parent != nullptr, "FS Truth: Parent must exist");

  bool removed = false;
  for (uint32_t i = 0; i < parent->child_count; i++) {
    if (parent->children[i] == node) {
      for (uint32_t j = i; j < parent->child_count - 1; j++)
        parent->children[j] = parent->children[j + 1];
      parent->child_count--;
      removed = true;
      break;
    }
  }

  CONTRACT_ASSERT(removed, "FS Truth: Failed to remove node");
  // Memory leak okay for truth model
  return true;
}

void fs_list_dir(const char *path) {
  FSNode *node = fs_resolve(path);
  if (!node) {
    serial_log("FS Truth: Path does not exist.");
    return;
  }
  serial_log("FS Truth: Listing directory...");
}

/* ======================================================
   STAGE 5 SELF-TEST
   ====================================================== */

void stage5_self_test() {
  serial_log("MODEL: Beginning Stage 5 Filesystem Truth Test...");
  CONTRACT_ASSERT(fs_mkdir("/", "model_test"), "FS Truth: mkdir failed");
  CONTRACT_ASSERT(fs_create_file("/model_test", "test.txt"),
                  "FS Truth: file create failed");

  FSNode *f = fs_resolve("/model_test/test.txt");
  CONTRACT_ASSERT(f != nullptr, "FS Truth: resolve failed");
  CONTRACT_ASSERT(f->type == NodeType::File, "FS Truth: wrong type");

  CONTRACT_ASSERT(fs_remove("/model_test/test.txt"), "FS Truth: remove failed");
  serial_log("MODEL: Stage 5 Filesystem Truth Passed.");
}

} // namespace FileSystemTruth

extern "C" void stage5_self_test() { FileSystemTruth::stage5_self_test(); }
