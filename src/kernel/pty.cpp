#include "pty.h"
#include "../include/errno.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "heap.h"
#include "memory.h"
#include "process.h"

extern "C" {

#define MAX_PTYS 16
static pty_t *pty_table[MAX_PTYS];

static int pty_fifo_write(pty_fifo_t *fifo, const char *buf, int len) {
  int written = 0;
  while (written < len) {
    if (fifo->count < PTY_BUFFER_SIZE) {
      fifo->buffer[fifo->head] = buf[written++];
      fifo->head = (fifo->head + 1) % PTY_BUFFER_SIZE;
      fifo->count++;
    } else {
      break; // Full
    }
  }
  if (written > 0)
    wake_up(&fifo->wait);
  return written;
}

static int pty_fifo_read(pty_fifo_t *fifo, char *buf, int len, bool nonblock) {
  if (fifo->count == 0) {
    if (nonblock)
      return -11; // -EAGAIN
    sleep_on(&fifo->wait);
  }
  int read_bytes = 0;
  while (read_bytes < len && fifo->count > 0) {
    buf[read_bytes++] = fifo->buffer[fifo->tail];
    fifo->tail = (fifo->tail + 1) % PTY_BUFFER_SIZE;
    fifo->count--;
  }
  return read_bytes;
}

// Slave TTY Write Callback: Data from Shell -> Terminal Emulator
static int pty_slave_tty_write(tty_t *tty, const char *buf, int len) {
  pty_t *pty = (pty_t *)tty->private_data;
  return pty_fifo_write(&pty->slave_to_master, buf, len);
}

// ============================================================================
// MASTER END OPERATIONS
// ============================================================================

static uint32_t pty_master_read(vfs_node_t *node, uint32_t offset,
                                uint32_t size, uint8_t *buffer) {
  (void)offset;
  pty_t *pty = (pty_t *)node->impl;
  return pty_fifo_read(&pty->slave_to_master, (char *)buffer, size,
                       pty->master_nonblock);
}

static uint32_t pty_master_write(vfs_node_t *node, uint32_t offset,
                                 uint32_t size, uint8_t *buffer) {
  (void)offset;
  pty_t *pty = (pty_t *)node->impl;
  // Master writes -> This is input for the slave (Shell)
  for (uint32_t i = 0; i < size; i++) {
    tty_input_char(&pty->slave_tty, buffer[i]);
  }
  return size;
}

// ============================================================================
// SLAVE END OPERATIONS
// ============================================================================

static uint32_t pty_slave_read(vfs_node_t *node, uint32_t offset, uint32_t size,
                               uint8_t *buffer) {
  (void)offset;
  pty_t *pty = (pty_t *)node->impl;
  // Note: tty_read currently blocks internally.
  // For now we'll just use it as is.
  return tty_read(&pty->slave_tty, (char *)buffer, size);
}

static uint32_t pty_slave_write(vfs_node_t *node, uint32_t offset,
                                uint32_t size, uint8_t *buffer) {
  (void)offset;
  pty_t *pty = (pty_t *)node->impl;
  return tty_write(&pty->slave_tty, (const char *)buffer, size);
}

// ============================================================================
// IOCTL
// ============================================================================
static int pty_ioctl(vfs_node_t *node, int request, void *argp) {
  pty_t *pty = (pty_t *)node->impl;
  bool is_master = (node->name[2] == 'm'); // ptm vs pts

  switch (request) {
  case 0x5421: // FIONBIO
    if (is_master)
      pty->master_nonblock = (argp && *(int *)argp);
    else
      pty->slave_nonblock = (argp && *(int *)argp);
    return 0;
  default:
    return -22; // -EINVAL
  }
}

static void pty_close(vfs_node_t *node) {
  pty_t *pty = (pty_t *)node->impl;
  if (!pty)
    return;

  bool is_master = (node->name[2] == 'm');
  if (is_master)
    pty->master_open = false;
  else
    pty->slave_open = false;

  if (!pty->master_open && !pty->slave_open) {
    // Both sides closed, free pty
    pty_table[pty->id] = 0;
    // Note: In a real system we'd also free the nodes if they aren't in the
    // pty_t but since they are, we must be careful with who frees what.
    // vfs_close will free the node after calling this.
    kfree(pty);
  }
}

// ============================================================================
// SYSCALL: sys_pty_create
// ============================================================================

int sys_pty_create(int *master_fd_out, int *slave_fd_out) {
  // Find free slot
  int pty_idx = -1;
  for (int i = 0; i < MAX_PTYS; i++) {
    if (!pty_table[i]) {
      pty_idx = i;
      break;
    }
  }
  if (pty_idx == -1)
    return -ENOMEM;

  pty_t *pty = (pty_t *)kmalloc(sizeof(pty_t));
  memset(pty, 0, sizeof(pty_t));
  pty->id = pty_idx;
  pty_table[pty_idx] = pty;
  pty->master_open = true;
  pty->slave_open = true;

  wait_queue_init(&pty->master_to_slave.wait);
  wait_queue_init(&pty->slave_to_master.wait);

  // Initialize Slave TTY
  memset(&pty->slave_tty, 0, sizeof(tty_t));
  strcpy(pty->slave_tty.name, "pts");
  itoa(pty_idx, pty->slave_tty.name + 3, 10);

  pty->slave_tty.flags = TTY_ECHO | TTY_CANON | TTY_ISIG;
  wait_queue_init(&pty->slave_tty.read_wait);
  pty->slave_tty.write_callback = pty_slave_tty_write;
  pty->slave_tty.private_data = pty;

  // Create VFS nodes
  vfs_node_t *master_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(master_node, 0, sizeof(vfs_node_t));
  strcpy(master_node->name, "ptm");
  itoa(pty_idx, master_node->name + 3, 10);
  master_node->flags = VFS_DEVICE;
  master_node->read = pty_master_read;
  master_node->write = pty_master_write;
  master_node->ioctl = pty_ioctl;
  master_node->close = pty_close;
  master_node->impl = pty;

  vfs_node_t *slave_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(slave_node, 0, sizeof(vfs_node_t));
  strcpy(slave_node->name, "pts");
  itoa(pty_idx, slave_node->name + 3, 10);
  slave_node->flags = VFS_DEVICE;
  slave_node->read = pty_slave_read;
  slave_node->write = pty_slave_write;
  slave_node->ioctl = pty_ioctl;
  slave_node->close = pty_close;
  slave_node->impl = pty;

  pty->master_node = master_node;
  pty->slave_node = slave_node;

  // Allocate file descriptors
  int mfd = -1, sfd = -1;
  for (int i = 0; i < MAX_PROCESS_FILES; i++) {
    if (!current_process->fd_table[i]) {
      if (mfd == -1)
        mfd = i;
      else if (sfd == -1) {
        sfd = i;
        break;
      }
    }
  }

  if (mfd == -1 || sfd == -1) {
    // Cleanup would go here
    return -EMFILE;
  }

  // Wrap in file_description_t
  file_description_t *mdesc =
      (file_description_t *)kmalloc(sizeof(file_description_t));
  mdesc->node = master_node;
  mdesc->offset = 0;
  mdesc->flags = 3; // O_RDWR
  mdesc->ref_count = 1;
  current_process->fd_table[mfd] = mdesc;

  file_description_t *sdesc =
      (file_description_t *)kmalloc(sizeof(file_description_t));
  sdesc->node = slave_node;
  sdesc->offset = 0;
  sdesc->flags = 3; // O_RDWR
  sdesc->ref_count = 1;
  current_process->fd_table[sfd] = sdesc;

  if (master_fd_out)
    *master_fd_out = mfd;
  if (slave_fd_out)
    *slave_fd_out = sfd;

  return 0;
}

void pty_init() {
  for (int i = 0; i < MAX_PTYS; i++)
    pty_table[i] = 0;
}

vfs_node_t *pty_get_slave_node(int index) {
  if (index < 0 || index >= MAX_PTYS || !pty_table[index])
    return nullptr;
  return pty_table[index]->slave_node;
}

int pty_get_max_ptys() { return MAX_PTYS; }

bool pty_is_active(int index) {
  return (index >= 0 && index < MAX_PTYS && pty_table[index] != nullptr);
}

} // extern "C"
