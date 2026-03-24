#include "../include/netfs.h"
#include "../drivers/serial.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"
#include "net_advanced.h"


// Simple HTTP-based Network File System
// Mounts a remote directory via HTTP (e.g., Python's http.server)

static struct vfs_node *netfs_root_node = nullptr;
static char remote_host[64];
static uint32_t remote_ip = 0;

static int netfs_read(struct vfs_node *node, uint64_t offset, void *buffer,
                      uint64_t size) {
  if (node->type == VFS_DIRECTORY)
    return -1;

  char url[512];
  // Build URL: http://<remote_ip>/<path>
  strcpy(url, "http://");
  strcat(url, remote_host);
  strcat(url, "/");
  strcat(url, node->name);

  serial_log("NETFS: Fetching ");
  serial_log(url);

  struct http_response resp;
  // We allocate a temporary buffer for the whole file because our HTTP stack is
  // simple In a real OS, we would use Range headers to fetch only required
  // chunks.
  uint8_t *tmp_buf = (uint8_t *)kmalloc(size + 4096);
  int received = http_get(url, tmp_buf, size + 4096, &resp);

  if (received >= 0 && (uint64_t)received >= offset) {
    uint64_t to_copy = size;
    if (offset + size > (uint64_t)received) {
      to_copy = (uint64_t)received - offset;
    }
    memcpy(buffer, tmp_buf + offset, to_copy);
    kfree(tmp_buf);
    return (int)to_copy;
  }

  kfree(tmp_buf);
  return -1;
}

static struct vfs_node *netfs_lookup(struct vfs_node *parent,
                                     const char *name) {
  // For now, we assume every file exists on the server to simplify the
  // "Installation" experience Real lookup would use HEAD or a directory index.
  struct vfs_node *node = (struct vfs_node *)kmalloc(sizeof(struct vfs_node));
  memset(node, 0, sizeof(struct vfs_node));

  strcpy(node->name, name);
  node->type = VFS_FILE;
  node->size = 0; // Unknown size until read
  node->fs = parent->fs;
  node->read = (uint32_t (*)(vfs_node *, uint32_t, uint32_t,
                             uint8_t *))netfs_read; // Legacy cast

  return node;
}

static struct filesystem netfs_fs = {
    "NETFS",
    nullptr, // mount (handled manually)
    netfs_lookup,
    nullptr, // create
    (int (*)(vfs_node *, uint64_t, void *, uint64_t))netfs_read,
    nullptr, // write
    nullptr, // close
    nullptr  // readdir
};

void netfs_init() { serial_log("NETFS: Initializing Network File System..."); }

struct vfs_node *netfs_mount(const char *host_ip, const char *path) {
  strcpy(remote_host, host_ip);
  remote_ip = ip_from_string(host_ip);

  struct vfs_node *node = (struct vfs_node *)kmalloc(sizeof(struct vfs_node));
  memset(node, 0, sizeof(struct vfs_node));

  strcpy(node->name, path);
  node->type = VFS_DIRECTORY;
  node->fs = &netfs_fs;

  serial_log("NETFS: Mounted at ");
  serial_log(host_ip);

  return node;
}
