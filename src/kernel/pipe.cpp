#include "pipe.h"
#include "../drivers/serial.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"
#include "process.h"

void pipe_init() {
  // Aage chalke kuch initialize karna ho toh
}

extern "C" {

uint32_t pipe_read(vfs_node_t *node, uint32_t offset, uint32_t size,
                   uint8_t *buffer) {
  (void)offset;
  pipe_t *pipe = (pipe_t *)node->impl;
  if (!pipe || pipe->read_closed)
    return 0;

  uint32_t read_bytes = 0;
  while (read_bytes < size) {
    if (pipe->head == pipe->tail) {
      if (pipe->write_closed)
        break;
      if (read_bytes > 0)
        break; // Jitna mila utna leke khush raho

      // Ruko zara, sabar karo (Data ka wait)
      current_process->state = PROCESS_WAITING;
      schedule();
      continue;
    }

    buffer[read_bytes++] = pipe->buffer[pipe->head];
    pipe->head = (pipe->head + 1) % PIPE_SIZE;
  }

  // Jo wait kar rahe hain unhe jagao (shayad koi likhne wala ho)
  process_t *p = ready_queue;
  if (p) {
    process_t *start = p;
    do {
      if (p->state == PROCESS_WAITING)
        p->state = PROCESS_READY;
      p = p->next;
    } while (p && p != start);
  }

  return read_bytes;
}

uint32_t pipe_write(vfs_node_t *node, uint32_t offset, uint32_t size,
                    uint8_t *buffer) {
  (void)offset;
  pipe_t *pipe = (pipe_t *)node->impl;
  if (!pipe || pipe->write_closed || pipe->read_closed)
    return 0;

  uint32_t written_bytes = 0;
  while (written_bytes < size) {
    uint32_t next_tail = (pipe->tail + 1) % PIPE_SIZE;
    if (next_tail == pipe->head) {
      if (written_bytes > 0)
        break; // Jitna likha gaya utna kaafi hai abhi ke liye

      // Jagah nahi hai, thoda ruko
      current_process->state = PROCESS_WAITING;
      schedule();
      continue;
    }

    pipe->buffer[pipe->tail] = buffer[written_bytes++];
    pipe->tail = next_tail;
  }

  // Padhne walon ko jagao, maal aa gaya hai
  process_t *p = ready_queue;
  if (p) {
    process_t *start = p;
    do {
      if (p->state == PROCESS_WAITING)
        p->state = PROCESS_READY;
      p = p->next;
    } while (p && p != start);
  }

  return written_bytes;
}

void pipe_close(vfs_node_t *node) {
  pipe_t *pipe = (pipe_t *)node->impl;
  if (!pipe)
    return;

  if (node->flags & 0x1)
    pipe->read_closed = 1;
  if (node->flags & 0x2)
    pipe->write_closed = 1;

  if (pipe->read_closed && pipe->write_closed) {
    kfree(pipe->buffer);
    kfree(pipe);
  }

  // Baakiyo ko batao ki dukaan band ho rahi hai ya khul rahi hai
  process_t *p = ready_queue;
  if (p) {
    process_t *start = p;
    do {
      if (p->state == PROCESS_WAITING)
        p->state = PROCESS_READY;
      p = p->next;
    } while (p && p != start);
  }
}

int sys_pipe(uint32_t *filedes) {
  if (!filedes)
    return -1;

  // Check karo pointer sahi hai ya nahi (basic wala)
  // Asli OS mein vm_verify_pointer use karte

  pipe_t *pipe = (pipe_t *)kmalloc(sizeof(pipe_t));
  if (!pipe)
    return -1;

  pipe->buffer = (uint8_t *)kmalloc(PIPE_SIZE);
  if (!pipe->buffer) {
    kfree(pipe);
    return -1;
  }

  pipe->head = 0;
  pipe->tail = 0;
  pipe->read_closed = 0;
  pipe->write_closed = 0;

  // Read end ke liye VFS node banao
  vfs_node_t *read_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(read_node, 0, sizeof(vfs_node_t));
  strcpy(read_node->name, "pipe_read");
  read_node->impl = (void *)pipe;
  read_node->read = pipe_read;
  read_node->close = pipe_close;
  read_node->flags = 0x1; // READ side
  read_node->ref_count = 1;

  // Write end ke liye VFS node banao
  vfs_node_t *write_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  memset(write_node, 0, sizeof(vfs_node_t));
  strcpy(write_node->name, "pipe_write");
  write_node->impl = (void *)pipe;
  write_node->write = pipe_write;
  write_node->close = pipe_close;
  write_node->flags = 0x2; // WRITE side
  write_node->ref_count = 1;

  // Process ke fd_table mein jagah dhundo
  int f1 = -1, f2 = -1;
  for (int i = 0; i < MAX_PROCESS_FILES; i++) {
    if (current_process->fd_table[i] == 0) {
      if (f1 == -1)
        f1 = i;
      else if (f2 == -1) {
        f2 = i;
        break;
      }
    }
  }

  if (f1 == -1 || f2 == -1) {
    kfree(read_node);
    kfree(write_node);
    kfree(pipe->buffer);
    kfree(pipe);
    return -1;
  }

  file_description_t *desc1 =
      (file_description_t *)kmalloc(sizeof(file_description_t));
  desc1->node = read_node;
  desc1->offset = 0;
  desc1->flags = 0; // O_RDONLY
  desc1->ref_count = 1;

  file_description_t *desc2 =
      (file_description_t *)kmalloc(sizeof(file_description_t));
  desc2->node = write_node;
  desc2->offset = 0;
  desc2->flags = 1; // O_WRONLY
  desc2->ref_count = 1;

  current_process->fd_table[f1] = desc1;
  current_process->fd_table[f2] = desc2;

  filedes[0] = (uint32_t)f1;
  filedes[1] = (uint32_t)f2;

  return 0;
}

} // extern "C"
